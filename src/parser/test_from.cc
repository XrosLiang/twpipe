#include <iostream>
#include <fstream>
#include "dynet/dynet.h"
#include "twpipe/logging.h"
#include "twpipe/embedding.h"
#include "twpipe/elmo.h"
#include "twpipe/model.h"
#include "twpipe/alphabet_collection.h"
#include "twpipe/corpus.h"
#include "twpipe/json.hpp"
#include "twpipe/ensemble.h"
#include "parser/tester.h"
#include "parser/parse_model_builder.h"
#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>

namespace po = boost::program_options;


void init_command_line(int argc, char* argv[], po::variables_map & conf) {
  po::options_description generic_opts("Generic options");
  generic_opts.add_options()
    ("mod", po::value<std::string>()->default_value("oracle"),
     "the mod of tester [oracle, vanilla, ensemble]")
    ("verbose,v", "details logging.")
    ("help,h", "show help information.")
    ("models", po::value<std::string>(), "the path to the models.")
    ("input-file", po::value<std::string>(), "the path to the input file.")
    ;

  po::options_description embed_opts = twpipe::WordEmbedding::get_options();
  po::positional_options_description input_opts;
  input_opts.add("input-file", -1);

  po::options_description cmd("Usage: ./test_from [running_opts] input-file");
  cmd.add(generic_opts)
    .add(embed_opts)
    ;

  po::store(po::command_line_parser(argc, argv).options(cmd).positional(input_opts).run(),
            conf);
  po::notify(conf);

  if (conf.count("help")) {
    std::cerr << cmd << std::endl;
    exit(1);
  }

  twpipe::init_boost_log(conf.count("verbose") > 0);

  if (!conf.count("input-file")) {
    std::cerr << "Please specify input file." << std::endl;
    exit(1);
  }
}

int main(int argc, char* argv[]) {
  dynet::initialize(argc, argv);

  po::variables_map conf;
  init_command_line(argc, argv, conf);

  if (conf.count("embedding")) {
    twpipe::WordEmbedding::get()->load(conf["embedding"].as<std::string>(),
                                       conf["embedding-dim"].as<unsigned>());
  } else {
    twpipe::WordEmbedding::get()->empty(conf["embedding-dim"].as<unsigned>());
  }

  if (conf.count("elmo")) {
    twpipe::ELMo::get()->load(conf["elmo"].as<std::string>(),
                              conf["elmo-dim"].as<unsigned>());
  } else {
    twpipe::ELMo::get()->empty(conf["elmo-dim"].as<unsigned>());
  }

  std::string payload = conf["models"].as<std::string>();
  std::vector<std::string> model_names;
  boost::split(model_names, payload, boost::is_any_of(","));
  unsigned n_models = model_names.size();

  std::vector<dynet::ParameterCollection *> models(n_models);
  std::vector<twpipe::ParseModel *> engines(n_models);
  for (unsigned i = 0; i < n_models; ++i) {
    twpipe::Model::get()->load(model_names[i]);
    if (i == 0) {
      twpipe::AlphabetCollection::get()->from_json();
    }

    if (!twpipe::Model::get()->has_parser_model()) {
      _ERROR << "[twpipe|parse|tester] doesn't have parser model!";
      continue;
    }
    twpipe::ParseModelBuilder par_builder(conf);
    models[i] = new dynet::ParameterCollection;
    engines[i] = par_builder.from_json(*models[i]);
  }

  std::string mod_name = conf["mod"].as<std::string>();
  twpipe::Tester * tester = nullptr;
  if (mod_name == "oracle") {
    tester = new twpipe::OracleTester(engines[0]);
  } else if (mod_name == "vanilla") {
    tester = new twpipe::VanillaTester(engines[0]);
  } else if (mod_name == "ensemble") {
    tester = new twpipe::EnsembleTester(engines);
  } else {
    _ERROR << "unknown mod name: " << mod_name;
    exit(1);
  }

  std::string buffer;
  std::vector<std::string> tokens;
  std::vector<std::string> postags;
  std::vector<unsigned> heads;
  std::vector<std::string> deprels;
  heads.push_back(twpipe::Corpus::BAD_HED);
  deprels.push_back(twpipe::Corpus::BAD0);
  std::vector<unsigned> actions;

  std::vector<std::vector<float>> prob;
  std::ifstream ifs(conf["input-file"].as<std::string>());

  unsigned sid = 0;
  while (std::getline(ifs, buffer)) {
    boost::algorithm::trim(buffer);
    if (buffer.empty()) {
      tester->test(tokens, postags, heads, deprels, actions, prob);

      if (!prob.empty()) {
        nlohmann::json output;
        output = {{twpipe::EnsembleInstance::id_name,       sid},
                  {twpipe::EnsembleInstance::category_name, actions},
                  {twpipe::EnsembleInstance::prob_name,     prob}};
        std::cout << output << std::endl;
      }
      tokens.clear();
      postags.clear();
      heads.clear();
      deprels.clear();
      actions.clear();
      heads.push_back(twpipe::Corpus::BAD_HED);
      deprels.emplace_back(twpipe::Corpus::BAD0);
      sid++;
    } else if (boost::algorithm::starts_with(buffer, "#ACTION ")) {
      actions.push_back(boost::lexical_cast<unsigned>(buffer.substr(8)));
    } else if (buffer[0] == '#') {
      continue;
    } else {
      std::vector<std::string> data;
      boost::algorithm::split(data, buffer, boost::is_any_of("\t "));
      tokens.push_back(data[1]);
      postags.push_back(data[3]);
      if (data[6] == "_") {
        heads.push_back(twpipe::Corpus::BAD_HED);
        deprels.emplace_back(twpipe::Corpus::BAD0);
      } else {
        heads.push_back(boost::lexical_cast<unsigned>(data[6]));
        deprels.push_back(data[7]);
      }
    }
  }
  _INFO << "[twpipe|parse|test] test " << sid + 1 << " instances.";
  return 0;
}
