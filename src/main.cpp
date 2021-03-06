#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"

#include "Utils.h"
using namespace std;

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

double distance(const double x1, const double y1,
								const double x2, const double y2)
{
	return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}

struct Path {
	vector<double> x;
	vector<double> y;
};

struct MapPath {
	vector<double> x;
	vector<double> y;
	vector<double> dx;
	vector<double> dy;
};

/**
 *
 * @param map_x
 * @param map_y
 * @param car_x
 * @param car_y
 * @param yaw
 * @return
 */
vector<double> mapXY2localXY(const double map_x, const double map_y,
								 const double car_x, const double car_y, const double yaw)
{
	double x = map_x - car_x;
	double y = map_y - car_y;

	return {x * cos(yaw) + y * sin(yaw),
					-x * sin(yaw) + y * cos(yaw)};
}

vector<double> localXY2mapXY(const double car_x, const double car_y,
														 const double l_x, const double l_y, const double yaw){
  return {l_x * cos(yaw) - l_y * sin(yaw) + car_x,
					l_y * sin(yaw) + l_y * cos(yaw) + car_y};
}

Path mapXYs2localXYs(Path global, const double car_x, const double car_y, const double car_yaw) {
	Path local_XYs;
  vector<double> local_xy;
	int size = global.x.size();
	for (auto i = 0; i < size; i+=1) {
		local_xy = mapXY2localXY(global.x[(int)i], global.y[(int)i], car_x, car_y, car_yaw);
    local_XYs.x.push_back(local_xy[0]);
		local_XYs.y.push_back(local_xy[1]);
	}
	return local_XYs;
}


int ClosestWaypoint(double x, double y, vector<double> maps_x, vector<double> maps_y)
{

	double closestLen = 100000; //large number
	int closestWaypoint = 0;

	for(int i = 0; i < maps_x.size(); i++)
	{
		double map_x = maps_x[i];
		double map_y = maps_y[i];
		double dist = distance(x,y,map_x,map_y);
		if(dist < closestLen)
		{
			closestLen = dist;
			closestWaypoint = i;
		}

	}

	return closestWaypoint;

}

int NextWaypoint(double x, double y, double theta, vector<double> maps_x, vector<double> maps_y)
{

	int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

	double map_x = maps_x[closestWaypoint];
	double map_y = maps_y[closestWaypoint];

	double heading = atan2( (map_y-y),(map_x-x) );

	double angle = abs(theta-heading);

	if(angle > pi()/4)
	{
		closestWaypoint++;
	}

	return closestWaypoint;

}

// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, vector<double> maps_x, vector<double> maps_y)
{
	int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

	int prev_wp;
	prev_wp = next_wp-1;
	if(next_wp == 0)
	{
		prev_wp  = maps_x.size()-1;
	}

	double n_x = maps_x[next_wp]-maps_x[prev_wp];
	double n_y = maps_y[next_wp]-maps_y[prev_wp];
	double x_x = x - maps_x[prev_wp];
	double x_y = y - maps_y[prev_wp];

	// find the projection of x onto n
	double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
	double proj_x = proj_norm*n_x;
	double proj_y = proj_norm*n_y;

	double frenet_d = distance(x_x,x_y,proj_x,proj_y);

	//see if d value is positive or negative by comparing it to a center point

	double center_x = 1000-maps_x[prev_wp];
	double center_y = 2000-maps_y[prev_wp];
	double centerToPos = distance(center_x,center_y,x_x,x_y);
	double centerToRef = distance(center_x,center_y,proj_x,proj_y);

	if(centerToPos <= centerToRef)
	{
		frenet_d *= -1;
	}

	// calculate s value
	double frenet_s = 0;
	for(int i = 0; i < prev_wp; i++)
	{
		frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
	}

	frenet_s += distance(0,0,proj_x,proj_y);

	return {frenet_s,frenet_d};

}

// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, vector<double> maps_s, vector<double> maps_x, vector<double> maps_y)
{
	int prev_wp = -1;

	while(s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1) ))
	{
		prev_wp++;
	}

	int wp2 = (prev_wp+1)%maps_x.size();

	double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),(maps_x[wp2]-maps_x[prev_wp]));
	// the x,y,s along the segment
	double seg_s = (s-maps_s[prev_wp]);

	double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
	double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

	double perp_heading = heading-pi()/2;

	double x = seg_x + d*cos(perp_heading);
	double y = seg_y + d*sin(perp_heading);

	return {x,y};

}

/**
 *
 * @param x
 * @param y
 * @param theta car_yaw
 * @param maps_x
 * @param maps_y
 * @return
 */
Path getAdjacentLocalWaypoints(const double x, const double y, const double theta, const double d,
																 MapPath path) {
  // ClosestWaypoints can also be used
	int next_wp = NextWaypoint(x, y, theta, path.x, path.y);
	int map_size = path.x.size();

	Path global_wps_segment;

  // Fitting using both past waypoints and future waypoints
	for (int i = -5, wp_id; i < 20; i+=1) {
    wp_id = (next_wp + i)%path.x.size();
		if (wp_id < 0) { wp_id += map_size; }
    global_wps_segment.x.push_back(path.x[wp_id] + d*path.dx[wp_id]);
		global_wps_segment.y.push_back(path.y[wp_id] + d*path.dy[wp_id]);

	}
  return global_wps_segment;
}

tk::spline fitLocalWaypoints(Path waypoints_segment) {
	tk::spline local_curve;
  local_curve.set_points(waypoints_segment.x, waypoints_segment.y);
	return local_curve;
}

int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  ifstream in_map_(map_file_.c_str(), ifstream::in);

  string line;
  while (getline(in_map_, line)) {
  	istringstream iss(line);
  	double x;
  	double y;
  	float s;
  	float d_x;
  	float d_y;
  	iss >> x;
  	iss >> y;
  	iss >> s;
  	iss >> d_x;
  	iss >> d_y;
  	map_waypoints_x.push_back(x);
  	map_waypoints_y.push_back(y);
  	map_waypoints_s.push_back(s);
  	map_waypoints_dx.push_back(d_x);
  	map_waypoints_dy.push_back(d_y);
  }

	// Logging
	string log_file = "../data/logger.csv";
	ofstream out_log(log_file.c_str(), ofstream::out);
	Utils utils;

  h.onMessage([&utils, &map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
        	// Main car's localization Data
					double car_x = j[1]["x"];
					double car_y = j[1]["y"];
          double car_s = j[1]["s"];
          double car_d = j[1]["d"];
          double car_yaw = j[1]["yaw"];
          double car_speed = j[1]["speed"];

          // Previous path data given to the Planner
          auto previous_path_x = j[1]["previous_path_x"];
          auto previous_path_y = j[1]["previous_path_y"];
          // Previous path's end s and d values
          double end_path_s = j[1]["end_path_s"];
          double end_path_d = j[1]["end_path_d"];

          // Sensor Fusion Data, a list of all other cars on the same side of the road.
          auto sensor_fusion = j[1]["sensor_fusion"];

          json msgJson;

          vector<double> next_x_vals;
          vector<double> next_y_vals;


          /***********************************************
					 * User code
					 ***********************************************/
          vector<double> map_in_local;

          int path_size = previous_path_x.size();
          double pos_x, pos_y, angle;
          // No previous points
					double num_steps = 50;
					// max speed ~ 49.75MPH
					const double max_diff = 0.445;
					vector<double> t;
          vector<double> dist_inc;
					tk::spline speed_curve;
					tk::spline local_curve;

					MapPath WayPoints = {map_waypoints_x, map_waypoints_y,
																 map_waypoints_dx, map_waypoints_dy};

					t.push_back(-1);
					t.push_back(6);
					t.push_back(12);
					t.push_back(25);
					t.push_back(num_steps);
					t.push_back(num_steps * 2);

					// TODO: fit another speed profile
					dist_inc.push_back(max_diff * 0.01);
					dist_inc.push_back(max_diff * 0.1);
					dist_inc.push_back(max_diff * 0.3);
					dist_inc.push_back(max_diff * 0.6);
					dist_inc.push_back(max_diff * 0.9);
					dist_inc.push_back(max_diff);

					speed_curve.set_points(t, dist_inc);

          if(path_size == 0)
          {
            pos_x = car_x;
            pos_y = car_y;
            angle = deg2rad(car_yaw);

						// Manually featured velocity profile
            // What if we use fitted s, x and s, y curve?
//						t.push_back(-1);
//						t.push_back(6);
//						t.push_back(12);
//						t.push_back(25);
//						t.push_back(num_steps);
//						t.push_back(num_steps * 2);
//
//						// TODO: fit another speed profile
//						dist_inc.push_back(max_diff * 0.01);
//						dist_inc.push_back(max_diff * 0.1);
//						dist_inc.push_back(max_diff * 0.3);
//						dist_inc.push_back(max_diff * 0.6);
//						dist_inc.push_back(max_diff * 0.8);
//						dist_inc.push_back(max_diff);
//
//            speed_curve.set_points(t, dist_inc);

						int lane = 1;
						double d = 2 + lane * 4;
						Path global_wps_segment =
										getAdjacentLocalWaypoints(pos_x, pos_y, angle, d, WayPoints);
						Path local_wps_segment = mapXYs2localXYs(global_wps_segment, pos_x, pos_y, angle);
						tk::spline local_curve = fitLocalWaypoints(local_wps_segment);

						double speed_inc;
						double next_x = 0, next_y;
						vector<double>globalXY;
						vector<double>prev_vd;
						for (auto i = 0; i < num_steps; i+=1) {
							speed_inc = speed_curve(0.0 + i);
							prev_vd.push_back(speed_inc);

							next_x += speed_inc * cos(angle);
              next_y = local_curve(next_x);
							cout << "Speed inc: " << speed_inc << endl;
//							out_log << "Speed inc" << speed_inc << endl;
							// TODO transfer back to global coordinates
							globalXY = localXY2mapXY(pos_x, pos_y, next_x, next_y, angle);
							next_x_vals.push_back(globalXY[0]);
							next_y_vals.push_back(globalXY[1]);
              cout << "next_x: " << globalXY[0] << endl;
							cout << "next_y: " << globalXY[1] << endl;
              // Do another filtering based on distance
						}
						cout << "<<<<<<<<<<<<<<<<<<<<<<<" << endl;
					} else {
						// After moving

						int path_size = previous_path_x.size();
						pos_x = previous_path_x[path_size-1];
						pos_y = previous_path_y[path_size-1];

						double pos_x2 = previous_path_x[path_size-2];
						double pos_y2 = previous_path_y[path_size-2];
						angle = atan2(pos_y-pos_y2,pos_x-pos_x2);

					/*	double pos_x = previous_path_x[0];
						double pos_y = previous_path_y[0];
						double pos_x2 = previous_path_x[1];
						double pos_y2 = previous_path_y[1];
						double angle = atan2(pos_y2 - pos_y, pos_x2 - pos_x);*/
            // Do another planning

						int lane = 1;
						double d = 2 + lane * 4;
						Path global_wps_segment =
										getAdjacentLocalWaypoints(pos_x, pos_y, angle, d, WayPoints);
						Path local_wps_segment = mapXYs2localXYs(global_wps_segment, car_x, car_y, car_yaw);
						/*for (auto i = 0; i < local_wps_segment.x.size(); i+=1) {
							cout << "local wps segment x: " << local_wps_segment.x[i] << endl;
							cout << "local wps segment y: " << local_wps_segment.y[i] << endl;
						}*/
						tk::spline local_curve = fitLocalWaypoints(local_wps_segment);

						double speed_inc;
						double next_x = 0, next_y;
						vector<double>globalXY;
						vector<double>prev_vd;
						for (auto i = 0; i < num_steps; i+=1) {
							speed_inc = speed_curve(0.0 + i);
							prev_vd.push_back(speed_inc);

							next_x += speed_inc * cos(angle);
              next_y = local_curve(next_x);
							cout << "Speed inc: " << speed_inc << endl;
							// TODO transfer back to global coordinates
							globalXY = localXY2mapXY(pos_x, pos_y, next_x, next_y, angle);
							next_x_vals.push_back(globalXY[0]);
							next_y_vals.push_back(globalXY[1]);
              cout << "next_x: " << globalXY[0] << endl;
							cout << "next_y: " << globalXY[1] << endl;
              // Do another filtering based on distance
						}
						cout << "<<<<<<<<<<<<<<<<<<<<<<<" << endl;
					}
          // TODO: define a path made up of (x,y) points that the car will visit sequentially every .02 seconds
          msgJson["next_x"] = next_x_vals;
          msgJson["next_y"] = next_y_vals;

          auto msg = "42[\"control\","+ msgJson.dump()+"]";

          //this_thread::sleep_for(chrono::milliseconds(1000));
          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
          
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
















































































