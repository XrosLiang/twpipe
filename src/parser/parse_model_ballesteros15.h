#ifndef __TWPIPE_PARSER_BALLESTEROS15_H__
#define __TWPIPE_PARSER_BALLESTEROS15_H__

#include "parse_model.h"
#include "state.h"
#include "system.h"
#include "twpipe/corpus.h"
#include "dynet_layer/layer.h"
#include <vector>
#include <unordered_map>
#include <boost/program_options.hpp>

namespace twpipe {

struct Ballesteros15Model : public ParseModel {
  struct StateCheckpointImpl : public StateCheckpoint {
    /// state machine
    ~StateCheckpointImpl() {}

    dynet::RNNPointer s_pointer;
    dynet::RNNPointer q_pointer;
    dynet::RNNPointer a_pointer;
    std::vector<dynet::Expression> stack;
    std::vector<dynet::Expression> buffer;
  };

  struct TransitionSystemFunction {
    virtual void perform_action(const unsigned& action,
                                dynet::ComputationGraph& cg,
                                LSTMBuilderType & a_lstm,
                                LSTMBuilderType & s_lstm,
                                LSTMBuilderType & q_lstm,
                                Merge3Layer& composer,
                                StateCheckpointImpl & checkpoint,
                                dynet::Expression& act_expr,
                                dynet::Expression& rel_expr) = 0;
  };

  struct ArcEagerFunction : public TransitionSystemFunction {
    void perform_action(const unsigned& action,
                        dynet::ComputationGraph& cg,
                        LSTMBuilderType & a_lstm,
                        LSTMBuilderType & s_lstm,
                        LSTMBuilderType & q_lstm,
                        Merge3Layer& composer,
                        StateCheckpointImpl & checkpoint,
                        dynet::Expression& act_expr,
                        dynet::Expression& rel_expr) override;

  };

  struct ArcStandardFunction : public TransitionSystemFunction {
    void perform_action(const unsigned& action,
                        dynet::ComputationGraph& cg,
                        LSTMBuilderType & a_lstm,
                        LSTMBuilderType & s_lstm,
                        LSTMBuilderType & q_lstm,
                        Merge3Layer& composer,
                        StateCheckpointImpl & checkpoint,
                        dynet::Expression& act_expr,
                        dynet::Expression& rel_expr) override;
  };

  struct ArcHybridFunction : public TransitionSystemFunction {
    void perform_action(const unsigned& action,
                        dynet::ComputationGraph& cg,
                        LSTMBuilderType & a_lstm,
                        LSTMBuilderType & s_lstm,
                        LSTMBuilderType & q_lstm,
                        Merge3Layer& composer,
                        StateCheckpointImpl & checkpoint,
                        dynet::Expression& act_expr,
                        dynet::Expression& rel_expr) override;
  };

  struct SwapFunction : public TransitionSystemFunction {
    void perform_action(const unsigned& action,
                        dynet::ComputationGraph& cg,
                        LSTMBuilderType & a_lstm,
                        LSTMBuilderType & s_lstm,
                        LSTMBuilderType & q_lstm,
                        Merge3Layer& composer,
                        StateCheckpointImpl & checkpoint,
                        dynet::Expression& act_expr,
                        dynet::Expression& rel_expr) override;
  };


  LSTMBuilderType fwd_ch_lstm;
  LSTMBuilderType bwd_ch_lstm;
  LSTMBuilderType s_lstm;
  LSTMBuilderType q_lstm;
  LSTMBuilderType a_lstm;

  SymbolEmbedding char_emb;
  SymbolEmbedding pos_emb;
  SymbolEmbedding act_emb;
  SymbolEmbedding rel_emb;
  InputLayer pretrain_emb;

  Merge3Layer merge_input;  // merge (2 * word, pos, preword)
  Merge3Layer merge;        // merge (s_lstm, q_lstm, a_lstm)
  Merge3Layer composer;     // compose (head, modifier, relation)
  DenseLayer scorer;

  dynet::Parameter p_action_start;  // start of action
  dynet::Parameter p_buffer_guard;  // end of buffer
  dynet::Parameter p_stack_guard;   // end of stack
  dynet::Parameter p_word_start_guard;
  dynet::Parameter p_word_end_guard;
  dynet::Parameter p_root_word;
  dynet::Expression action_start;
  dynet::Expression buffer_guard;
  dynet::Expression stack_guard;
  dynet::Expression word_start_guard;
  dynet::Expression word_end_guard;
  dynet::Expression root_word;

  /// state machine
  dynet::RNNPointer s_pointer;
  dynet::RNNPointer q_pointer;
  dynet::RNNPointer a_pointer;
  std::vector<dynet::Expression> stack;
  std::vector<dynet::Expression> buffer;

  /// The reference
  TransitionSystemFunction* sys_func;

  /// The Configurations: useful for other models.
  unsigned size_c, dim_c, dim_w, size_p, dim_p, dim_t, size_a, dim_a, dim_l;
  unsigned n_layers, dim_lstm_in, dim_hidden;

  explicit Ballesteros15Model(dynet::ParameterCollection & m,
                              unsigned size_c,
                              unsigned dim_c,
                              unsigned dim_w,   // word size, word dim
                              unsigned size_p,
                              unsigned dim_p,   // pos size, pos dim
                              unsigned dim_t,   // pword size, pword dim
                              unsigned size_a,  //
                              unsigned dim_a,   // act size, act dim
                              unsigned dim_l,
                              unsigned n_layers,
                              unsigned dim_lstm_in,
                              unsigned dim_hidden,
                              TransitionSystem& system,
                              EmbeddingType embedding_type);

  void new_graph(dynet::ComputationGraph& cg) override;

  void initialize_parser(dynet::ComputationGraph& cg,
                         const InputUnits& input,
                         StateCheckpoint * checkpoint) override;

  void perform_action(const unsigned& action,
                      const State& state,
                      dynet::ComputationGraph& cg,
                      StateCheckpoint * checkpoint) override;

  StateCheckpoint * get_initial_checkpoint() override;

  StateCheckpoint * copy_checkpoint(StateCheckpoint * checkpoint) override;

  void destropy_checkpoint(StateCheckpoint * checkpoint) override;

  /// Get the un-softmaxed scores from the LSTM-parser.
  dynet::Expression get_scores(StateCheckpoint * checkpoint) override;

  dynet::Expression l2() override;
};

}

#endif  //  end for PARSER_H
