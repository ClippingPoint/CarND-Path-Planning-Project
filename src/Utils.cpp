//
// Created by ian zhang on 7/31/17.
//

#include "Utils.h"

vector<double> Utils::map2car(loc_t map, loc_t ego, double yaw) {
  double x = map.x - ego.x;
  double y = map.y - ego.y;

  return {x * cos(-yaw) - y * sin(-yaw),
          x * sin(-yaw) + y * cos(-yaw)};
}

/**
 * Back to the global, only translational relation is required
 * @param ego
 * @param map
 * @return
 */
vector<double> Utils::car2map(loc_t ego, loc_t local) {
  double x_diff = ego.x - local.x;
  double y_diff = ego.y - local.y;

  return { local.x + x_diff, local.y + y_diff };
}
