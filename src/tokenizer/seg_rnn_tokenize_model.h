#ifndef __TWPIPE_SEGMENTAL_RNN_TOKENIZE_MODEL_H__
#define __TWPIPE_SEGMENTAL_RNN_TOKENIZE_MODEL_H__

#include <regex>
#include "tokenize_model.h"
#include "twpipe/alphabet_collection.h"

namespace twpipe {

template <class RNNBuilderType>
struct SegmentalRNNTokenizeModel : public TokenizeModel {
  const static char* name;
  BiRNNLayer<RNNBuilderType> bi_rnn;
  SegBiRNN<RNNBuilderType> seg_rnn;
  BinnedDurationEmbedding dur_embed;
  SymbolEmbedding char_embed;
  Merge2Layer merge;
  Merge3Layer merge3;
  DenseLayer dense;

  unsigned char_size;
  unsigned char_dim;
  unsigned hidden_dim;
  unsigned n_layers;
  unsigned seg_dim;
  unsigned dur_dim;
  unsigned max_seg_len;

  std::regex one_more_space_regex;

  SegmentalRNNTokenizeModel(dynet::ParameterCollection & model,
                            unsigned char_size,
                            unsigned char_dim,
                            unsigned hidden_dim,
                            unsigned n_layers,
                            unsigned seg_dim,
                            unsigned dur_dim,
                            unsigned max_seg_len=25) :
    TokenizeModel(model),
    bi_rnn(model, n_layers, char_dim, hidden_dim),
    seg_rnn(model, n_layers, hidden_dim, seg_dim, max_seg_len),
    dur_embed(model, dur_dim),
    char_embed(model, char_size, char_dim),
    merge(model, hidden_dim, hidden_dim, hidden_dim),
    merge3(model, seg_dim, seg_dim, dur_dim, seg_dim),
    dense(model, seg_dim, 1),
    char_size(char_size),
    char_dim(char_dim),
    hidden_dim(hidden_dim),
    n_layers(n_layers),
    seg_dim(seg_dim),
    dur_dim(dur_dim),
    max_seg_len(max_seg_len),
    one_more_space_regex("[ ]{2,}") {

  }

  void new_graph(dynet::ComputationGraph & cg) {
    bi_rnn.new_graph(cg);
    seg_rnn.new_graph(cg);
    dur_embed.new_graph(cg);
    char_embed.new_graph(cg);
    merge.new_graph(cg);
    merge3.new_graph(cg);
    dense.new_graph(cg);
  }

  void decode(const std::string & input, std::vector<std::string> & output) {
    Alphabet & char_map = AlphabetCollection::get()->char_map;
    dynet::ComputationGraph * cg = merge.B.pg;
    std::string clean_input = std::regex_replace(input, one_more_space_regex, " ");

    std::vector<unsigned> cids;
    std::vector<std::string> chars;

    unsigned len = 0;
    for (unsigned i = 0; i < clean_input.size(); i += len) {
      len = utf8_len(clean_input[i]);
      std::string ch = clean_input.substr(i, len);
      chars.push_back(ch);
      unsigned cid = (char_map.contains(ch) ? char_map.get(ch) : char_map.get(Corpus::UNK));
      cids.push_back(cid);
    }

    unsigned n_chars = cids.size();
    std::vector<dynet::Expression> ch_exprs(n_chars);
    for (unsigned i = 0; i < n_chars; ++i) {
      ch_exprs[i] = char_embed.embed(cids[i]);
    }
    bi_rnn.add_inputs(ch_exprs);

    std::vector<BiRNNOutput> hiddens1;
    bi_rnn.get_outputs(hiddens1);

    std::vector<dynet::Expression> c(n_chars);
    for (unsigned i = 0; i < n_chars; ++i) {
      c[i] = dynet::rectify(merge.get_output(hiddens1[i].first, hiddens1[i].second));
    }
    seg_rnn.construct_chart(c);

    std::vector<dynet::Expression> alpha(n_chars + 1);
    std::vector<dynet::Expression> f;
    std::vector<std::pair<unsigned, unsigned>> ijt;
    std::vector<std::pair<unsigned, unsigned>> it; // for recording the gold

    it.push_back(std::make_pair(0, 0));
    for (unsigned j = 1; j <= n_chars; ++j) {
      f.clear();
      ijt.clear();
      unsigned i_start = (j < max_seg_len ? 0 : j - max_seg_len);
      for (unsigned i = i_start; i < j; ++i) {
        dynet::Expression p = factor_score(i, j, false);
        if (i == 0) {
          f.push_back(p);
        } else {
          f.push_back(p + alpha[i]);
        }
        ijt.push_back(std::make_pair(i, j));
      }
      BOOST_ASSERT_MSG(f.size() > 0, "There should be a result!");
      unsigned max_id = 0;
      float max_val = dynet::as_scalar(cg->get_value(f[0]));
      for (unsigned id = 1; id < f.size(); ++id) {
        auto val = dynet::as_scalar(cg->get_value(f[id]));
        if (max_val < val) { max_val = val; max_id = id; }
      }
      alpha[j] = f[max_id];
      it.push_back(ijt[max_id]);
    }

    auto cur_j = n_chars;
    while (cur_j > 0) {
      auto cur_i = std::get<0>(it[cur_j]);
      bool all_space = true;
      for (unsigned i = cur_i; i < cur_j; ++i) {  if (cids[i] != space_cid) { all_space = false; break; } }
      if (!all_space) {
        std::string word = "";
        for (unsigned i = cur_i; i < cur_j; ++i) {
          word += chars[i];
        }
        output.push_back(word);
      }
      cur_j = cur_i;
    }
    std::reverse(output.begin(), output.end());
  }

  dynet::Expression objective(const Instance & inst) {
    Alphabet & char_map = AlphabetCollection::get()->char_map;
    const InputUnits & input_units = inst.input_units;
    std::string clean_input = std::regex_replace(inst.raw_sentence, one_more_space_regex, " ");
     
    std::vector<unsigned> segmentation; 
    std::vector<unsigned> cids;
    unsigned len = 0;
    unsigned j = 1, k = 0;
    for (unsigned i = 0; i < clean_input.size(); i += len) {
      len = utf8_len(clean_input[i]);
      std::string ch = clean_input.substr(i, len);
      unsigned cid = char_map.get(ch);
      cids.push_back(cid);
      if (cid != space_cid) {
        ++k;
        if (k == input_units[j].cids.size()) {
          segmentation.push_back(input_units[j].cids.size());
          k = 0;
          ++j;
        }
      } else {
        segmentation.push_back(1);
      }
    }

    unsigned n_chars = cids.size();
    std::vector<std::vector<bool>> is_ref(n_chars, std::vector<bool>(n_chars + 1, false));
    unsigned cur = 0;
    for (unsigned ri = 0; ri < segmentation.size(); ++ri) {
      BOOST_ASSERT_MSG(cur < n_chars, "[tokenize|model] segment index greater than sentence length.");
      unsigned dur = segmentation[ri];
      if (dur > max_seg_len) {
        // _ERROR << "[tokenize|model] max_seg_len=" << max_seg_len << " but reference duration is " << dur;
        return dynet::zeroes((*merge.B.pg), {1});
        // abort();
      }
      unsigned j = cur + dur;
      BOOST_ASSERT_MSG(j <= n_chars, "[tokenize|model] end of segment is greater than the input sentence.");
      is_ref[cur][j] = true;
      cur = j;
    }

    std::vector<dynet::Expression> ch_exprs(n_chars);
    for (unsigned i = 0; i < n_chars; ++i) {
      ch_exprs[i] = char_embed.embed(cids[i]);
    }
    bi_rnn.add_inputs(ch_exprs);

    std::vector<BiRNNOutput> hiddens1;
    bi_rnn.get_outputs(hiddens1);

    std::vector<dynet::Expression> c(n_chars);
    for (unsigned i = 0; i < n_chars; ++i) {
      c[i] = dynet::rectify(merge.get_output(hiddens1[i].first, hiddens1[i].second));
    }
    seg_rnn.construct_chart(c);

    // f is the expression of overall matrix, fr is the expression of reference.
    std::vector<dynet::Expression> alpha(n_chars + 1), ref_alpha(n_chars + 1);
    std::vector<dynet::Expression> f;
    for (unsigned j = 1; j <= n_chars; ++j) {
      f.clear();
      unsigned i_start = max_seg_len ? (j < max_seg_len ? 0 : j - max_seg_len) : 0;
      for (unsigned i = i_start; i < j; ++i) {
        bool matches_ref = is_ref[i][j];
        dynet::Expression p = factor_score(i, j, true);

        if (i == 0) {
          f.push_back(p);
          if (matches_ref) { ref_alpha[j] = p; }
        } else {
          f.push_back(p + alpha[i]);
          if (matches_ref) { ref_alpha[j] = p + ref_alpha[i]; }
        }
      }
      alpha[j] = dynet::logsumexp(f);
    }
    return alpha.back() - ref_alpha.back();
  }

  dynet::Expression l2() {
    std::vector<dynet::Expression> ret;
    for (auto & e : bi_rnn.get_params()) { ret.push_back(dynet::squared_norm(e)); }
    for (auto & e : seg_rnn.get_params()) { ret.push_back(dynet::squared_norm(e)); }
    for (auto & e : merge.get_params()) { ret.push_back(dynet::squared_norm(e)); }
    for (auto & e : merge3.get_params()) { ret.push_back(dynet::squared_norm(e)); }
    return dynet::sum(ret);
  }

  dynet::Expression factor_score(unsigned i, unsigned j, bool ) {
    auto seg_ij = seg_rnn(i, j - 1);
    auto dur = dur_embed.embed(j - i);
    return dense.get_output(
      dynet::rectify(merge3.get_output(seg_ij.first, seg_ij.second, dur)));
  }
};

typedef SegmentalRNNTokenizeModel<dynet::GRUBuilder> SegmentalGRUTokenizeModel;
typedef SegmentalRNNTokenizeModel<dynet::CoupledLSTMBuilder> SegmentalLSTMTokenizeModel;

}

#endif  //  end for __TWPIPE_SEGMENTAL_RNN_TOKENIZE_MODEL_H__