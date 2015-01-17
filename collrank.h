#ifndef _COLLRANK_H
#define _COLLRANK_H

using namespace std;

struct rating
{
	int user_id;
	int item_id;
	int score;

	rating(): user_id(0), item_id(0), score(0) {}
	rating(int u, int i, int s): user_id(u), item_id(i), score(s) {}
	void setvalues(const int u, const int i, const int s) {
		user_id = u;
		item_id = i;
		score = s;
	}
	void swap(rating& r) {
		int temp;
		temp = user_id; user_id = r.user_id; r.user_id = temp;
		temp = item_id; item_id = r.item_id; r.item_id = temp;
		temp = score; score = r.score; r.score = temp;
	}
};

struct ratingf
{
	int user_id;
	int item_id;
	double score;

	ratingf(): user_id(0), item_id(0), score(0.) {}
	ratingf(int u, int i, double s): user_id(u), item_id(i), score(s) {}
	void setvalues(const int u, const int i, const double s) {
		user_id = u;
		item_id = i;
		score = s;
	}
	void swap(ratingf& r) {
		int temp;
		temp = user_id; user_id = r.user_id; r.user_id = temp;
		temp = item_id; item_id = r.item_id; r.item_id = temp;
    double tempf;
		tempf = score; score = r.score; r.score = tempf;
	}
};

struct comparison
{
	int user_id;
	int item1_id;
	int item2_id;
  int comp;

	comparison(): user_id(0), item1_id(0), item2_id(0), comp(1) {}
	comparison(int u, int i1, int i2, int cp): user_id(u), item1_id(i1), item2_id(i2), comp(cp) {}
	comparison(const comparison& c): user_id(c.user_id), item1_id(c.item1_id), item2_id(c.item2_id), comp(c.comp) {}
	void setvalues(const int u, const int i1, const int i2, const int cp) {
		user_id = u;
		item1_id = i1;
		item2_id = i2;
    comp = cp;
	}
	void swap(comparison& c) {
		int temp;
		temp = user_id; user_id = c.user_id; c.user_id = temp;
		temp = item1_id; item1_id = c.item1_id; c.item1_id = temp;
		temp = item2_id; item2_id = c.item2_id; c.item2_id = temp;
	  temp = comp; comp = c.comp; c.comp = temp;
  }
};

bool comp_userwise(comparison a, comparison b) { return ((a.user_id < b.user_id) || ((a.user_id == b.user_id) && (a.item1_id < b.item1_id))); }
bool comp_itemwise(comparison a, comparison b) { return ((a.item1_id < b.item1_id) || ((a.item1_id == b.item1_id) && (a.user_id < b.user_id))); }

bool comp_ratingwise(rating a, rating b) { return (a.score > b.score); }
bool rate_userwise(rating a, rating b) { return ((a.user_id < b.user_id) || ((a.user_id == b.user_id) && (a.item_id < b.item_id))); }
bool ratef_userwise(ratingf a, ratingf b) { return ((a.user_id < b.user_id) || ((a.user_id == b.user_id) && (a.item_id < b.item_id))); }
bool ratef_ratingwise(ratingf a, ratingf b) { return (a.score > b.score); }

typedef struct rating rating;
typedef struct ratingf ratingf;
typedef struct comparison comparison;

/*Parallal Collaborative Ranking, collaborative project by Jin Zhang and Dohyung Park
Input: 
	**x,	2D arrary of feature_node, index :  used by w, 
	l,	row number of X, or number of training samples
	n,	column number, or dimension of each training samples
	C,	coefficient
Output:
	w,	feature vector, with dimension n

using dual coordinate descent with the equation listed in the report
*/

#endif
