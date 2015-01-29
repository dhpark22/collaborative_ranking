#ifndef __PK_HPP__
#define __PK_HPP__

#include <utility>
#include <vector>
#include <queue>
#include <iostream>
#include <fstream>
#include <unordered_set>

#include "model.hpp"

class Evaluator {
  
  Evaluator();
  virtual void evaluate(const Model&);

};

class EvaluatorBinary : public Evaluator {
  
  // please add your structure to store the datasets

  public:
    void load_files();
}

class EvaluatorRating : public Evaluator {

  RatingMatrix test;

  public:
    void load_files();  
}

void EvaluateRating::evaluate(const Model& model) {

}

struct pkcomp {
	bool operator() (std::pair<int, double> i, std::pair<int, double> j) {
		return i.second > j.second;
	}
};

void EvaluateBinary::load_files (char* train_repo, char* test_repo, std::vector<int>& k) {

} 

void EvaluateBinary::evaluate (const Model& model) {
	std::vector<std::unordered_set<int> > train, test;	
	train.resize(model.n_users);
	test.resize(model.n_users);
	
	std::ifstream tr(train_repo);
	if (tr) {
		int uid, iid;
		while (tr >> uid >> iid) {
			train[uid - 1].insert(iid - 1);
		}
	} else {
		printf ("Error in opening the training repository!\n");
		exit(EXIT_FAILURE);
	}
	tr.close();

	std::ifstream te(test_repo);
	if (te) {
		int uid, iid;
		while (te >> uid >> iid) {
			test[uid - 1].insert(iid - 1);
		}
	} else {
		printf ("Error in opening the testing repository!\n");
		exit(EXIT_FAILURE);
	}
	te.close();

	int maxK = k[k.size() - 1];
	std::vector<double> ret(k.size(), 0);
	std::priority_queue<std::pair<int, double>, std::vector<std::pair<int, double> >, pkcomp> pq;	

	for (int i = 0; i < model.n_users; ++i) {
		for (int j = 0; j < model.n_items; ++j) {
			if (train[i].find(j) == train[i].end() ) {
				continue;
			}

			double score = 0;
			double *user_vec = &model.U[i * model.rank];
			double *item_vec = &model.V[j * model.rank];
			for (int l = 0; l < model.rank; ++l) {
				score += user_vec[l] * item_vec[l];
			}

			if (pq.size() < maxK) {
				pq.push(std::pair<int, double>(j, score) );
			} else if (pq.top().second < score) {
				pq.push(std::pair<int, double>(j, score) );
				pq.pop();	
			}
		}

		int ps = pq.size();
		while (ps) {
			int item = pq.top().first;
			for (int j = k.size() - 1; j >= 0; --j) {
				if (ps > k[j]) break;
				if (test[i].find(item) != test[i].end() ) ++ret[j];
			}
			pq.pop();
			--ps;
		}
	}

	for (int i = 0; i < k.size(); ++i) {
		ret[i] = ret[i] / model.n_users / k[i];
	}
	return ret;
}

#endif