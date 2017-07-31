//
// Created by ian zhang on 7/31/17.
//

#ifndef PATH_PLANNING_UTILS_H
#define PATH_PLANNING_UTILS_H

#include <vector>
#include <math.h>
#include "Constants.h"

using namespace std;

class Utils {
public:
  vector<double> map2car(loc_t map, loc_t ego, double yaw);
  vector<double> car2map(loc_t ego, loc_t local);
};


#endif //PATH_PLANNING_UTILS_H
