// Project: Parallel Collaborative Ranking with Alt-rankSVM and SGD
// Collaborative work by Dohyung Park and Jin Zhang
// Date: 11/26/2014
//
// The script will:
// [1]  convert preference data into item-based graph (adjacency matrix format)
// [2]  partition graph with Graclus
// [3a] solve the problem with alternative rankSVM via liblineaer
// [3b] solve the problem with stochasitic gradient descent in hogwild style
// [3c] solve the problem with stochastic gradient descent in nomad style
//
// Compile: g++ -std=C++11 -O3 -g -fopenmp collaborative_ranking.cpp
// Run: ./a.out [rating_file] [rating_format] [graph_output] [num_partitions]

#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <map>
#include <vector>
#include <iostream>
#include <fstream>
#include "linear.h"
#include "collaborative_ranking.h"
#include "cdSVM.h"

using namespace std;

class Problem {
    bool is_allocated, is_clustered;
    int n_users, n_items, n_train_comps, n_test_comps; 	// number of users/items in training sample, number of samples in traing and testing data set
    int rank, lambda, nparts;				// parameters
    double *U, *V;						// low rank U, V
    double alpha, beta;					// parameter for sgd
    Graph g;						// Graph used for clustering training data
    vector<int> n_comps_by_user, n_comps_by_item;
    vector<comparison> comparisons_test;			// vector stores testing comparison data

    bool sgd_step(const comparison &comp, double step_size);
    void de_allocate();					// deallocate U, V when they are used multiple times by different methods
  
  public:
    Problem(int, int);				// default constructor
    ~Problem();					// default destructor
    void read_data(char* train_file, char* test_file);	// read function
    void alt_rankSVM();
    void run_sgd_random();
    void run_sgd_nomad();
    double compute_ndcg();
    double compute_testerror();
};

// may be more parameters can be specified here
Problem::Problem (int r, int np): g(np) {
	this->rank = r;
	this->is_allocated = false;
	this->is_clustered = false;
	this->nparts = np;
}

Problem::~Problem () {
	//printf("calling the destructor\n");
	this->de_allocate();
}

void Problem::read_data (char* train_file, char* test_file) {
	this->g.read_data(train_file);	// training file will be feed to graph for clustering
	this->n_users = this->g.n;
	this->n_items = this->g.m;
	this->n_train_comps = this->g.omega;

	n_comps_by_user.clear(); n_comps_by_user.resize(this->n_users);
	n_comps_by_item.clear(); n_comps_by_item.resize(this->n_items);
	for(int i=0; i<this->n_users; i++) n_comps_by_user[i] = 0;
	for(int i=0; i<this->n_items; i++) n_comps_by_item[i] = 0;

	for(int i=0; i<this->n_train_comps; i++) {
		++n_comps_by_user[g.ucmp[i].user_id];
		++n_comps_by_item[g.ucmp[i].item1_id];
		++n_comps_by_item[g.ucmp[i].item2_id];
	}		

	ifstream f(test_file);
	if (f) {
		int u, i, j;
		while (f >> u >> i >> j) {
			this->n_users = max(u, this->n_users);
			this->n_items = max(i, max(j, this->n_items));
			this->comparisons_test.push_back(comparison(u - 1, i - 1, j - 1) );
		}
		this->n_test_comps = this->comparisons_test.size();
	} else {
		printf("error in opening the testing file\n");
		exit(EXIT_FAILURE);
	}
	f.close();

    printf("%d users, %d items, %d training comps, %d test comps \n", this->n_users, this->n_items,
                                                                      this->n_train_comps,
                                                                      this->n_test_comps);

	this->U = new double [this->n_users * this->rank];
	this->V = new double [this->n_items * this->rank];

    /*
	if (!is_clustered) {
		this->g.cluster();		// call graph clustering prior to the computation
		is_clustered = true;
	}
    */
}

void Problem::alt_rankSVM () {

	if (!is_clustered) {
		this->g.cluster();		// call graph clustering prior to the computation
		is_clustered = true;
	}
 
	srand(time(NULL));
	for (int i = 0; i < this->n_users * this->rank; ++i) {
		this->U[i] = ((double) rand() / RAND_MAX);
	}
	for (int i = 0; i < this->n_items * this->rank; ++i) {
		this->V[i] = ((double) rand() / RAND_MAX);
	}

	printf("initial error %f\n", this->compute_testerror() );

	// Alternating RankSVM
	struct feature_node **A, **B;
	A = new struct feature_node*[this->n_train_comps];
	for (int i = 0; i < this->n_train_comps; ++i) {
		A[i] = new struct feature_node[this->rank + 1];
		for (int j = 0; j < this->rank; ++j) {
			A[i][j].index = j + 1;
		}
		A[i][this->rank].index = -1;
	}

	B = new struct feature_node*[this->n_train_comps];
	for (int i = 0; i < this->n_train_comps; ++i) {
		B[i] = new struct feature_node[this->rank * 2 + 1];
		for (int j = 0; j < 2 * this->rank; ++j) {
			B[i][j].index = j + 1;
		}
		B[i][this->rank * 2].index = -1;
	}

	for (int iter = 0; iter < 20; ++iter) {
		// Learning U
		#pragma omp parallel for
		for (int i = 0; i < this->n_users; ++i) {
			for (int j = this->g.uidx[i]; j < this->g.uidx[i + 1]; ++j) {
				double *V1 = &V[this->g.ucmp[j].item1_id * this->rank];
				double *V2 = &V[this->g.ucmp[j].item2_id * this->rank];
				for (int s = 0; s < this->rank; ++s) {
					A[j][s].value = V1[s] - V2[s];
				}
			}

			// call LIBLINEAR with U[i * rank]
			struct problem P;
			P.l = this->g.uidx[i + 1] - this->g.uidx[i];
			P.n = this->rank;
			double *y = new double[P.l];
			for (int j = 0; j < P.l; ++j) {
				y[j] = 1.;
			}
			P.y = y;
			P.x = &A[this->g.uidx[i]];
			P.bias = -1.;

			struct parameter param;
			param.solver_type = L2R_L2LOSS_SVC_DUAL;
			param.C = 1.;
			param.eps = 1e-8;
			//struct model *M;
			if (!check_parameter(&P, &param) ) {
				// run SVM
				//M = train(&P, &param);
				vector<double> w = trainU(&P, &param);
				// store the result
				for (int j = 0; j < rank; ++j) {
					this->U[i * rank + j] = w[j];
				}
				//free_and_destroy_model(&M);
			}
			delete [] y;
		}

		// Learning V 
		#pragma omp parallel for
		for (int i = 0; i < this->nparts; ++i) {
			// solve the SVM problem sequentially for each sample in the partition
			for (int j = this->g.pidx[i]; j < this->g.pidx[i + 1]; ++j) {
				// generate the training set for V using U
				for (int s = 0; s < this->rank; ++s) {
					B[j][s].value = U[this->g.pcmp[j].user_id * this->rank + s];		// U_i
					B[j][s + rank].value = -U[this->g.pcmp[j].user_id * this->rank + s];	// -U_i
				}		
			
				// call LIBLINEAR with U[i*rank], B[j]
				struct problem P;
				P.l = 1;
				P.n = rank * 2;
				double y = 1.;
				P.y = &y;
				P.x = &B[j];
				P.bias = -1;

				struct parameter param;
				param.solver_type = L2R_L2LOSS_SVC_DUAL;
				param.C = 1.;
				param.eps = 1e-8;
				//struct model *M;
				if (!check_parameter(&P, &param) ) {
					// run SVM
					//M = train(&P, &param);
					vector<double> w = trainV(&P, &param);

					// store the result
					for (int s = 0; s < rank; ++s) {
						int v1 = this->g.pcmp[j].item1_id;
						int v2 = this->g.pcmp[j].item2_id;
						this->V[this->g.pcmp[j].item1_id * this->rank + s] = w[s];			// other threads might be doing the same thing
						this->V[this->g.pcmp[j].item2_id * this->rank + s] = w[s + this->rank];		// so add lock to the two steps is another option.
					}
					//free_and_destroy_model(&M);
				}
			}
		}
		printf("iteratrion %d, test error %f\n", iter, this->compute_testerror() );
	}

	for (int i = 0; i < this->n_train_comps; ++i) {
		delete [] A[i];
		delete [] B[i];
	}
	delete [] A;
	delete [] B;
}	

bool Problem::sgd_step(const comparison& comp, const double step_size) {
	double *user_vec  = &U[comp.user_id * rank];
	double *item1_vec = &V[comp.item1_id * rank];
	double *item2_vec = &V[comp.item2_id * rank];

    int n_comps_user  = n_comps_by_user[comp.user_id];
    int n_comps_item1 = n_comps_by_item[comp.item1_id];
    int n_comps_item2 = n_comps_by_item[comp.item2_id];

	double err = 1.;
	for(int k=0; k<rank; k++) err -= user_vec[k] * (item1_vec[k] - item2_vec[k]);

	if (err > 0) {	
		double grad = -2 * err;		// gradient direction for l2 hinge loss

		for(int k=0; k<rank; k++) {
			double user_dir  = (grad * (item1_vec[k] - item2_vec[k]) + lambda / n_comps_user * user_vec[k]);
			double item1_dir = (grad * user_vec[k] + lambda / n_comps_item1 * item1_vec[k]);
			double item2_dir = (-grad * user_vec[k] + lambda / n_comps_item2 * item2_vec[k]);

            //#pragma omp atomic
			user_vec[k]  -= step_size * user_dir;

            //#pragma omp atomic
			item1_vec[k] -= step_size * item1_dir;

            //#pragma omp atomic
			item2_vec[k] -= step_size * item2_dir;
		}

		return true;
	}

	return false;
}

void Problem::run_sgd_random() {

    printf("%d \n", RAND_MAX);

	srand(static_cast<unsigned>(time(NULL)));
	for(int i=0; i<n_users*rank; i++) U[i] = ((double)rand()/(RAND_MAX));
	for(int i=0; i<n_items*rank; i++) V[i] = ((double)rand()/(RAND_MAX));

    /*
    for(int i=0; i<n_items; i++)
        if (n_comps_by_item[i] == 0)
            printf("%d \n", i);
    */

    alpha = 1.;
    beta  = .1;
    lambda = .1;

    int n_threads = g.nparts-1;
    int n_iter = n_train_comps*100/n_threads;
    
    #pragma omp parallel
    {

	for(int iter=1; iter<n_iter; iter++) {
		int idx = (int)((double)rand() * (double)n_train_comps / (double)RAND_MAX);

//		sgd_step(g.ucmp[idx], alpha / (1. + beta * (double)iter));

        comparison comp = g.ucmp[idx];
        double step_size = alpha / (1. + beta * (double)iter);

    	double *user_vec  = &U[comp.user_id * rank];
    	double *item1_vec = &V[comp.item1_id * rank];
    	double *item2_vec = &V[comp.item2_id * rank];

        int n_comps_user  = n_comps_by_user[comp.user_id];
        int n_comps_item1 = n_comps_by_item[comp.item1_id];
        int n_comps_item2 = n_comps_by_item[comp.item2_id];

    	double err = 1.;
    	for(int k=0; k<rank; k++) err -= user_vec[k] * (item1_vec[k] - item2_vec[k]);
        
    	if (err > 0) {	
    		double grad = -2 * err;		// gradient direction for l2 hinge loss

	    	for(int k=0; k<rank; k++) {
		    	double user_dir  = step_size * (grad * (item1_vec[k] - item2_vec[k]) + lambda / n_comps_user * user_vec[k]);
		    	double item1_dir = step_size * (grad * user_vec[k] + lambda / n_comps_item1 * item1_vec[k]);
	    		double item2_dir = step_size * (-grad * user_vec[k] + lambda / n_comps_item2 * item2_vec[k]);

                //#pragma omp atomic
		    	user_vec[k]  -= user_dir;

                //#pragma omp atomic
		    	item1_vec[k] -= item1_dir;

                //#pragma omp atomic
			    item2_vec[k] -= item2_dir;
	    	}
        }

        if ((iter % (n_train_comps*10/n_threads) == 1) && (omp_get_thread_num() == 0)) {
            /*
            printf("%d %d %d %d %d %d %f \n", g.ucmp[idx].user_id,  n_comps_by_user[g.ucmp[idx].user_id],
                                              g.ucmp[idx].item1_id, n_comps_by_item[g.ucmp[idx].item1_id],
                                              g.ucmp[idx].item2_id, n_comps_by_item[g.ucmp[idx].item2_id],
                                              alpha / (1. + beta*(double)iter));
            
            for(int k=0; k<rank; k++) printf("%5.2f ", U[g.ucmp[idx].user_id+k]); printf("\n");
            for(int k=0; k<rank; k++) printf("%5.2f ", V[g.ucmp[idx].item1_id+k]); printf("\n");
            for(int k=0; k<rank; k++) printf("%5.2f ", V[g.ucmp[idx].item2_id+k]); printf("\n");
            
            for(int k=0; k<rank; k++) printf("%5.2f ", U[k]); printf("\n");
            for(int k=0; k<rank; k++) printf("%5.2f ", V[k]); printf("\n");
            */ 
            
            printf("%d: %d iteration, %f test error\n", omp_get_thread_num(), iter, this->compute_testerror());
            //printf("%d: %d iteration\n", omp_get_thread_num(), iter);
        }
	}

    }

}

void Problem::run_sgd_nomad() {

    if (!is_clustered) {
		this->g.cluster();		// call graph clustering prior to the computation
		is_clustered = true;
	}

	srand(time(NULL));
	for(int i=0; i<n_users*rank; i++) U[i] = ((double)rand()/(RAND_MAX));
	for(int i=0; i<n_items*rank; i++) V[i] = ((double)rand()/(RAND_MAX));

    int n_iter = 10;
	for(int iter=1; iter<n_iter; iter++) {
		int idx = (int)((double)rand() * (double)n_train_comps / (double)RAND_MAX);

		sgd_step(g.pcmp[idx], alpha / (1. + beta * (double)iter));

		double ndcg = compute_ndcg();
		double test_err = compute_testerror();
	}

}

double Problem::compute_ndcg() {
  double ndcg_sum = 0.;
  for(int i=0; i<n_users; i++) {
    double dcg = 0.;
    double norm = 1.;
    // compute dcg
    ndcg_sum += dcg / norm;
  }
}

double Problem::compute_testerror() {
  int n_error = 0;
  for(int i=0; i<n_test_comps; i++) {
    double prod = 0.;
    int user_idx  = comparisons_test[i].user_id * rank;
    int item1_idx = comparisons_test[i].item1_id * rank;
    int item2_idx = comparisons_test[i].item2_id * rank;
    for(int k=0; k<rank; k++) prod += U[user_idx + k] * (V[item1_idx + k] - V[item2_idx + k]);
    if (prod <= 0.) ++n_error;
    if (prod != prod) {
      printf("NaN detected \n");
      return 1.;
    }

    /*
    if (i == 0) {
      for(int k=0; k<rank; k++) printf("%5.2f ", U[user_idx+k]); printf("\n");
      for(int k=0; k<rank; k++) printf("%5.2f ", V[item1_idx+k]); printf("\n");
      for(int k=0; k<rank; k++) printf("%5.2f ", V[item2_idx+k]); printf("\n");
      printf("%d %d %d %5.2f \n", comparisons_test[i].user_id,
                                  comparisons_test[i].item1_id,
                                  comparisons_test[i].item2_id,
                                  prod);
    }
    */
  }
  //printf("%d %d\n", n_error, n_test_comps);
  return (double)n_error / (double)n_test_comps;
}

void Problem::de_allocate () {
  delete [] this->U;
  delete [] this->V;
  this->U = NULL;
  this->V = NULL;
}

int main (int argc, char* argv[]) {
	if (argc < 4) {
		cout << "Solve collaborative ranking problem with given training/testing data set" << endl;
		cout << "Usage ./collaborative_ranking  : [training file] [testing file] [num_threads]" << endl;
		return 0;
	}

	int nr_threads = atoi(argv[3]);
	//int nparts = (nr_threads > 1) ? nr_threads : 2;
	Problem p(10, nr_threads + 1);		// rank = 10, #partition = 16
    p.read_data(argv[1], argv[2]);
	omp_set_dynamic(0);
	omp_set_num_threads(nr_threads);
	double start = omp_get_wtime();

	//p.alt_rankSVM();
	
    p.run_sgd_random();

    double end = omp_get_wtime() - start;
	printf("%d threads, %f error, takes %f seconds\n", nr_threads, p.compute_testerror(), end);
	return 0;
}
