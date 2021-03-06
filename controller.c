/* 
 *	SAILBOAT-CONTROLLER
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <complex.h>
#include <stdbool.h>

#define MAINSLEEP_SEC	0		// seconds
#define MAINSLEEP_MSEC	250		// milliseconds
#define MAXLOGLINES	30000
#define SEC		4.0		// number of loops per second
#define PI 		3.14159265

#define TACKINGRANGE 	100		// meters
#define RADIUSACCEPTED	5		// meters
#define CONVLON		64078		// meters per degree
#define CONVLAT		110742		// meters per degree

#define theta_nogo	55*PI/180	// [radians] Angle of nogo zone, compared to wind direction
#define theta_down	30*PI/180 	// [radians] Angle of downwind zone, compared to wind direction.
#define v_min		0.5		// [meters/seconds] Min velocity for tacking
#define angle_lim 	5*PI/180	// [degrees] threshold for jibing. The heading has to be 5 degrees close to desired Heading.
#define ROLL_LIMIT 	15		// [degrees] Threshold for an automatic emergency sail release

#define dtime		120		// [seconds] Duty Cycle Period

#define INTEGRATOR_MAX	20		// [degrees], influence of the integrator
#define RATEOFTURN_MAX	36		// [degrees/second]
#define dHEADING_MAX	10		// [degrees] deviation, before rudder PI acts
#define GAIN_P 		-1
#define GAIN_I 		0

#define BoomLength	1.6 		// [meters] Length of the Boom
#define SCLength	1.43 		// [meters] horizontal distance between sheet hole and mast
#define SCHeight	0.6		// [meters] vertical distance between sheet hole and boom.
#define strokelength	0.5		// [meters] actuator length

// Sail Hill Climbing
#define SAIL_ACT_TIME  3		// [seconds] actuation time of the sail hillclimbing algoritm
#define SAIL_OBS_TIME  20		// [seconds] observation time of the sail hillclimbing algoritm
#define ACT_MAX		870		// [ticks] the max number of actuator ticks
#define SAIL_LIMIT	150		// [ticks] max tolerated difference between desired and current actuator position
#define MAX_DUTY_CYCLE 	0.6     	// [%] Datasheet max duty cycle
#define ACT_PRECISION	20		// [ticks] how close the actuator gets to the Sail_Desired_Position

// Simulation constants
#define SIM_SOG		8		// [meters/seconds] boat speed over ground during simulation
#define SIM_ROT		5		// [degrees/seconds] rate of turn
#define SIM_ACT_INC	160		// [millimiters/seconds] sail actuator increment per second

#include "map_geometry.h"		// custom functions to handle geometry transformations on the map


FILE* file;
float Rate=0, Heading=270, Deviation=0, Variation=0, Yaw=0, Pitch=0, Roll=0;
float Latitude=0, Longitude=0, COG=0, SOG=0, Wind_Speed=0, Wind_Angle=0;
float Point_Start_Lat=0, Point_Start_Lon=0, Point_End_Lat=0, Point_End_Lon=0;
int   Rudder_Desired_Angle=0,   Manual_Control_Rudder=0, Rudder_Feedback=0;
int   Sail_Desired_Position=0,  Manual_Control_Sail=0,   Sail_Feedback=0, desACTpos=0;
int   Navigation_System=0, Prev_Navigation_System=0, Manual_Control=0, Simulation=0;
int   logEntry=0, fa_debug=0, debug=0, debug2=0, debug3=0, debug4=0, debug5=0, debug6=0, debug_hc=0;
int   debug_jibe=0;
char  logfile1[50],logfile3[50];  //logfile2[50],

void initfiles();
void check_navigation_system();
void onNavChange();
void read_weather_station();
void read_weather_station_essential();
void read_external_variables();
void read_sail_position();
void read_target_point();
void move_rudder(int angle);
void move_sail(int position);
void write_log_file();
int  sign(float val);
void simulate_sailing();
void meanwind();
void  countFCN();
float power(float number, float eksponent);

struct timespec timermain;


//guidance
float _Complex X, X_T, X_T_b, X_b, X0;
float _Complex X1, X2, X3, X4;		// Tacking boundary end points
float _Complex Geo_X1, Geo_X2, Geo_X3, Geo_X4;
float integratorSum=0, Guidance_Heading=0, override_Guidance_Heading=-1;
float theta=0, theta_b=0, theta_d=0, theta_d_b=0, theta_d1=0, theta_d1_b=0, a_x=0, b_x=0;
float theta_pM=0, theta_pM_b=0, theta_d_out=0, theta_mean_wind=0;
int   sig = 0, sig1 = 0, sig2 = 0, sig3 = 0; // coordinating the guidance
int   roll_counter = 0, tune_counter = 0, counter = 0;
int   jibe_status = 1, actIn;

// GUI inputs from "read_external_variables"
int sail_state=1, heading_state=1;		// variables defining sail tune and heading algorithm state
int steptime=30, stepsize=10, vLOS=0, stepDIR=1, DIR_init=0, des_heading=100, des_app_w=100, sail_stepsize=5, sail_pos=0;
float des_slope=0.0015;

void guidance();
void findAngle();
void chooseManeuver();
void performManeuver();
void rudder_pid_controller();
void jibe_pass_fcn();

void sail_controller();

void heading_hc_slope_controller();
void heading_hc_controller();
void heading_hc_heeling_controller();
void stepheading();
void sail_hc_controller();
int signfcn(float);


// heading hill climbing
float v_poly=0;	// Simulation variable, defining the velocity using a polynome.
float heel_sim=0;
float ctri_heel=0;
float ctri_headsl=0;
float ctri_head=0;
float ctri_sail=0;
int u_sail=0;		// u_sail=0 is equal to a sail angle close to zero.
int u_headsl = 180;	// Initialized u_headsl going southwards
int u_head = 180;
int u_heel = 180;
//int hc_head=0;


// Stepheading variable
int headstep=180;//Wind_Angle+180;		// Initially on downwind course

//waypoints
int nwaypoints=0, current_waypoint=0;
Point AreaWaypoints[1000], Waypoints[1000];
int calculate_area_waypoints();
int prepare_waypoint_array();

int main(int argc, char ** argv) {
	
	// set timers
	timermain.tv_sec  = MAINSLEEP_SEC;
	timermain.tv_nsec = MAINSLEEP_MSEC * 1000000L;

	initfiles();
	fprintf(stdout, "\nSailboat-controller running..\n");
	read_weather_station();

	// MAIN LOOP
	while (1) {

		// read GUI configuration files (navigation system and manual control values)
		check_navigation_system(); if (Navigation_System != Prev_Navigation_System) onNavChange();


		if (Manual_Control) {

			move_rudder(Manual_Control_Rudder);		// Move the rudder to user position
			desACTpos = Manual_Control_Sail;		// Move the main sail to user position
			read_weather_station_essential();

		} else {

			// AUTOPILOT ON
			if ((Navigation_System==1)||(Navigation_System==3))
			{
				read_weather_station();			// Update sensors data
				read_external_variables();
				read_sail_position();			// Read sail actuator feedback
				meanwind();
				countFCN();
				switch(heading_state)
				{
					case 1:
						guidance();				// Calculate the desired heading
						break;
					case 2:
						heading_hc_slope_controller();
						
						break;
					case 3:
						heading_hc_controller();		// Execute the heading hillclimbing algorithm
						break;
					case 4:					
						stepheading();
						break;
					case 5:
						break;
					case 6:
						heading_hc_heeling_controller();
						break;
					default:
						if(debug) printf("heading_state switch case error.");
				}
				rudder_pid_controller();		// Calculate the desired rudder position
				
				switch(sail_state)
				{
					case 1:
						desACTpos = ACT_MAX/strokelength/2*(sqrt( SCLength*SCLength + BoomLength*BoomLength - 2*SCLength*BoomLength*cos(sail_pos*PI/180) + SCHeight*SCHeight)-sqrt( SCLength*SCLength + BoomLength*BoomLength -2*SCLength*BoomLength*cos(0) + SCHeight*SCHeight));
						break;
					case 2:
						sail_hc_controller();			// Execute the sail hillclimbing algorithm
						break;
					case 3:
						if (debug_jibe) printf("actIn before sail controller: %d \n", actIn);
						sail_controller();			// Execute the default sail controller
						if (debug_jibe) printf("desACTpos after sail controller: %d \n", desACTpos);
						break;
				}
				move_sail(desACTpos);

				if(Simulation) simulate_sailing();

				// reaching the waypoint
				if  ( (cabs(X_T - X) < RADIUSACCEPTED) && (Navigation_System==1) )
				{	
					// switch to maintain position
					file = fopen("/tmp/sailboat/Navigation_System", "w");
					if (file != NULL) { fprintf(file, "3");	fclose(file); }
				}
			}
			else
			{
				// AUTOPILOT OFF
				read_weather_station_essential();
			}
		}

		// write a log line
		write_log_file();

		//sleep
		nanosleep(&timermain, (struct timespec *)NULL);
	}
	return 0;
}

/*
 *	Initialize system files and create folder structure
 */
void initfiles() {
	system("mkdir -p /tmp/sailboat");
	system("mkdir -p sailboat-log/debug/");
	system("mkdir -p sailboat-log/thesis/");

	system("cp /usr/share/wp_go     /tmp/sailboat/");
	system("cp /usr/share/wp_return /tmp/sailboat/");
	system("cp /usr/share/area_vx   /tmp/sailboat/");
	system("cp /usr/share/area_int  /tmp/sailboat/");

	system("[ ! -f /tmp/sailboat/Navigation_System ] 	&& echo 0 > /tmp/sailboat/Navigation_System");
	system("[ ! -f /tmp/sailboat/Navigation_System_Rudder ] && echo 0 > /tmp/sailboat/Navigation_System_Rudder");
	system("[ ! -f /tmp/sailboat/Navigation_System_Sail ] 	&& echo 0 > /tmp/sailboat/Navigation_System_Sail");
	system("[ ! -f /tmp/sailboat/Manual_Control ] 		&& echo 0 > /tmp/sailboat/Manual_Control");
	system("[ ! -f /tmp/sailboat/Manual_Control_Rudder ] 	&& echo 0 > /tmp/sailboat/Manual_Control_Rudder");
	system("[ ! -f /tmp/sailboat/Manual_Control_Sail ] 	&& echo 0 > /tmp/sailboat/Manual_Control_Sail");
	system("[ ! -f /tmp/sailboat/Point_Start_Lat ] 		&& echo 0 > /tmp/sailboat/Point_Start_Lat");
	system("[ ! -f /tmp/sailboat/Point_Start_Lon ] 		&& echo 0 > /tmp/sailboat/Point_Start_Lon");
	system("[ ! -f /tmp/sailboat/Point_End_Lat ] 		&& echo 0 > /tmp/sailboat/Point_End_Lat");
	system("[ ! -f /tmp/sailboat/Point_End_Lon ] 		&& echo 0 > /tmp/sailboat/Point_End_Lon");
	system("[ ! -f /tmp/sailboat/Guidance_Heading ] 	&& echo 0 > /tmp/sailboat/Guidance_Heading");
	system("[ ! -f /tmp/sailboat/Rudder_Feedback ] 		&& echo 0 > /tmp/sailboat/Rudder_Feedback");
	system("[ ! -f /tmp/sailboat/Sail_Feedback ] 		&& echo 0 > /tmp/sailboat/Sail_Feedback");
	system("[ ! -f /tmp/sailboat/Simulation ] 		&& echo 0 > /tmp/sailboat/Simulation");
	system("[ ! -f /tmp/sailboat/Simulation_Wind ] 		&& echo 0 > /tmp/sailboat/Simulation_Wind");
	system("[ ! -f /tmp/sailboat/boundaries ] 		&& echo 0 > /tmp/sailboat/boundaries");
	
	// Additional GUI input data for Hill Climbing Thesis
	system("[ ! -f /tmp/sailboat/duty ] 			&& echo 0 > /tmp/sailboat/duty");
	system("[ ! -f /tmp/sailboat/mean_wind ]		&& echo 0 > /tmp/sailboat/mean_wind");
	system("[ ! -f /tmp/sailboat/Sail_Feedback ]		&& echo 0 > /tmp/sailboat/Sail_Feedback");
	system("[ ! -f /tmp/sailboat/u_sail ]		&& echo 0 > /tmp/sailboat/u_sail");

	system("[ ! -f /tmp/sailboat/override_Guidance_Heading ] && echo -1 > /tmp/sailboat/override_Guidance_Heading");
}

/*
 *	Check Navigation System Status
 *
 *	[Navigation System] 
 *		- [0] Boat in IDLE status
 *		- [1] Control System ON, Sail to the waypoint
 *		- [3] Control System ON, Mantain position
 *		- [4] Calculate Route
 *	[Manual Control]
 *		- [0] OFF
 *		- [1] User takes control of sail and rudder positions
 *
 *	if Manual_Control is ON, read the following values:
 *		- [Manual_Control_Rudder] : user value for desired RUDDER angle [-30.0 to 30.0]
 *		- [Manual_Control_Sail]   : user value for desired SAIL position [0 to 500] 
 */
void check_navigation_system() {

	file = fopen("/tmp/sailboat/Navigation_System", "r");
	if (file != NULL) { fscanf(file, "%d", &Navigation_System); fclose(file); }

	file = fopen("/tmp/sailboat/Manual_Control", "r");
	if (file != NULL) { fscanf(file, "%d", &Manual_Control); fclose(file); }

	if(Manual_Control) {

		file = fopen("/tmp/sailboat/Manual_Control_Rudder", "r");
		if (file != NULL) { fscanf(file, "%d", &Manual_Control_Rudder); fclose(file); }

		file = fopen("/tmp/sailboat/Manual_Control_Sail", "r");
		if (file != NULL) { fscanf(file, "%d", &Manual_Control_Sail); fclose(file);}
	}

	file = fopen("/tmp/sailboat/Simulation", "r");
	if (file != NULL) { fscanf(file, "%d", &Simulation); fclose(file); }
}


/*
 *	Whenever a new Navigation_System state is entered:
 */
void onNavChange() {

	// Update current position, wind angle, heading, ...
	read_weather_station();

	// SWITCH AUTOPILOT OFF
	if(Navigation_System==0) {

		// do nothing
	}


	// START SAILING
	if(Navigation_System==1) {

		// read target point from file 
		read_target_point();

		// update starting point
		Point_Start_Lat=Latitude;
		Point_Start_Lon=Longitude;
		file = fopen("/tmp/sailboat/Point_Start_Lat", "w");
		if (file != NULL) { fprintf(file, "%f", Point_Start_Lat); fclose(file); }
		file = fopen("/tmp/sailboat/Point_Start_Lon", "w");
		if (file != NULL) { fprintf(file, "%f", Point_Start_Lon); fclose(file); }
	}


	// MAINTAIN POSITION
	if(Navigation_System==3) {				
		
		Point_End_Lon = Longitude;			// Overwrite the END point coord. using the current position
		Point_End_Lat = Latitude;
		file = fopen("/tmp/sailboat/Point_End_Lat", "w");
		if (file != NULL) { fprintf(file, "%f", Point_End_Lat); fclose(file); }
		file = fopen("/tmp/sailboat/Point_End_Lon", "w");
		if (file != NULL) { fprintf(file, "%f", Point_End_Lon); fclose(file); }
	}
	

	// CALCULATE ROUTE
	if(Navigation_System==4) {

		// do nothing, route calculation has been removed, go to "start sailing"

		// Start sailing	
		file = fopen("/tmp/sailboat/Navigation_System", "w");	
		if (file != NULL) { fprintf(file, "1"); fclose(file); }	
	}
	

	// update the navigation system status
	Prev_Navigation_System=Navigation_System;
}




/*
 *	GUIDANCE V3:
 *
 *	Calculate the desired Heading based on the WindDirection, StartPoint, EndPoint, CurrentPosition and velocity.
 *
 *	This version is able to perform tacking and jibing. Compared to V1, it needs another input theta, which is the
 *	current heading of the vessel. Also it has two more outputs: 'sig' and 'dtheta'. They are used in tacking situations,
 *	putting the rudder on the desired angle. This leads to changes in the rudder-pid-controller, see below.
 *
 *	Daniel Wrede, May 2013
 */
void guidance() 
{
	// GUIDANCE V3: nogozone, tack and jibe capable solution
	//  - Let's try to keep the order using sig,sig1,sig2,sig3 and theta_d,theta_d1. theta_d_b is actually needed in the chooseManeuver function.
	//  - lat and lon translation would be better on the direct input
	if (debug) printf("*********** Guidance **************** \n");
	float x, y, theta_wind;
	float _Complex Geo_X, Geo_X0, Geo_X_T;
	char boundaries[200];


	//if (debug) printf("theta_d: %4.1f [deg]\n",theta_d*180/PI);

	x=Longitude;
	y=Latitude;
	theta=Heading*PI/180;
	theta_wind=Wind_Angle*PI/180;

	// complex notation for x,y position of the starting point
	Geo_X0 = Point_Start_Lon + 1*I*Point_Start_Lat;
	X0 = 0 + 1*I*0;

	// complex notation for x,y position of the boat
	Geo_X = x + 1*I*y;
	X=(Geo_X-Geo_X0);
	X=creal(X)*CONVLON + I*cimag(X)*CONVLAT;

	// complex notation for x,y position of the target point
	Geo_X_T = Point_End_Lon + I*Point_End_Lat;
	X_T=(Geo_X_T-Geo_X0);
	X_T=creal(X_T)*CONVLON + I*cimag(X_T)*CONVLAT;
	if (debug) printf("Point_End_Lon: %f \n",Point_End_Lon);
	if (debug) printf("Point_End_Lat: %f \n",Point_End_Lat);

	// ** turning matrix **

	// The calculations in the guidance system are done assuming constant wind
	// from above. To make this system work, we need to 'turn' it according to
	// the wind direction. It is like looking on a map, you have to find the
	// north before reading it.

	// Using theta_wind to transfer X_T. Here theta_wind is expected to be zero
	// when coming from north, going clockwise in radians.
	X_T_b = ccos(atan2(cimag(X_T),creal(X_T))+theta_wind)*cabs(X_T) + 1*I*(csin(atan2(cimag(X_T),creal(X_T))+theta_wind)*cabs(X_T));
	if (debug) printf("X_T_b: %f + I*%f \n",creal(X_T_b),cimag(X_T_b));
	
	X_b = ccos(atan2(cimag(X),creal(X))+theta_wind)*cabs(X) + 1*I*(csin(atan2(cimag(X),creal(X))+theta_wind)*cabs(X));
	theta_b = theta_wind - theta + PI/2;
	if (debug_jibe) printf("init SIG:[%d]\n",sig);

	if (sig == 0)  
	{
		findAngle();
		if (debug) printf("findAngle SIG1:[%d]\n",sig1);
                if (sig1 == 1) { chooseManeuver(); if (debug) printf("chooseManeuver SIG2:[%d]\n",sig2); }
		else { sig2 = sig1; }
	}
	else
	{
		theta_d1_b = theta_d_b;
                sig1 = sig;		
                sig2 = sig1;
	}

	//if (debug) printf("theta_d1: %4.1f deg. \n",theta_d1);

        if (sig2 > 0) { performManeuver(); if (debug) printf("performManeuver SIG3:[%d]\n",sig3); }
	else { sig3 = sig2; }

	if (debug5) printf("SIG1: [%d] - SIG2: [%d] - SIG3: [%d] \n",sig1, sig2, sig3);

	// Updating the history angle, telling the guidance heading from last iteration
	theta_d_b = theta_d1_b;
	sig = sig3;

	// Inverse turning matrix
	theta_d1 = theta_d1_b-theta_wind;
	theta_pM = theta_pM_b-theta_wind;

	// Finding Geo_Xn. 3 Steps. Correcting for wind, CONVLON/LAT, geographic location
	X1 = ccos(atan2(cimag(X1),creal(X1))-theta_wind)*cabs(X1) + 1*I*(csin(atan2(cimag(X1),creal(X1))-theta_wind)*cabs(X1));
	X1 = creal(X1)/CONVLON + I*cimag(X1)/CONVLAT;
	Geo_X1 = X1 + Geo_X0;
	
	X2 = ccos(atan2(cimag(X2),creal(X2))-theta_wind)*cabs(X2) + 1*I*(csin(atan2(cimag(X2),creal(X2))-theta_wind)*cabs(X2));
	X2 = creal(X2)/CONVLON + I*cimag(X2)/CONVLAT;
	Geo_X2 = X2 + Geo_X0;

	X3 = ccos(atan2(cimag(X3),creal(X3))-theta_wind)*cabs(X3) + 1*I*(csin(atan2(cimag(X3),creal(X3))-theta_wind)*cabs(X3));
	X3 = creal(X3)/CONVLON + I*cimag(X3)/CONVLAT;
	Geo_X3 = X3 + Geo_X0;

	X4 = ccos(atan2(cimag(X4),creal(X4))-theta_wind)*cabs(X4) + 1*I*(csin(atan2(cimag(X4),creal(X4))-theta_wind)*cabs(X4));
	X4 = creal(X4)/CONVLON + I*cimag(X4)/CONVLAT;
	Geo_X4 = X4 + Geo_X0;

	//if (debug3) printf("\nTacking boundary end points:\n");
	//if (debug3) printf("GeoX0: %f + I*%f \n",creal(Geo_X0),cimag(Geo_X0));
	//if (debug3) printf("GeoXT: %f + I*%f \n",creal(Geo_X_T),cimag(Geo_X_T));
	//if (debug3) printf("GeoX1: %f + I*%f \n",creal(Geo_X1),cimag(Geo_X1));
	//if (debug3) printf("GeoX2: %f + I*%f \n",creal(Geo_X2),cimag(Geo_X2));
	//if (debug3) printf("GeoX3: %f + I*%f \n",creal(Geo_X3),cimag(Geo_X3));
	//if (debug3) printf("GeoX4: %f + I*%f \n",creal(Geo_X4),cimag(Geo_X4));


	if ( sig3>0 ) { theta_d_out = theta_pM; }
	else { theta_d_out = theta_d1; }
	Guidance_Heading = (PI/2 - theta_d_out) * 180/PI; 

	if (debug) printf("FA_DEBUG:[%d]\n",fa_debug);

	// write guidance_heading to file to be displayed in GUI 
	file = fopen("/tmp/sailboat/Guidance_Heading", "w");
	if (file != NULL) {
		fprintf(file, "%4.1f", Guidance_Heading);
		fclose(file);
	}

	// if we are in SIMULATION MODE, write boundaries to file to be displayed in the GUI
	if(Simulation) {
		sprintf(boundaries, "%.6f;%.6f,%.6f;%.6f,%.6f;%.6f,%.6f;%.6f,",cimag(Geo_X1),creal(Geo_X1),cimag(Geo_X2),creal(Geo_X2),cimag(Geo_X3),creal(Geo_X3),cimag(Geo_X4),creal(Geo_X4));
		file = fopen("/tmp/sailboat/boundaries", "w");
		if (file != NULL) {
			fprintf(file, "%s\n", boundaries);
			fclose(file);
		}
	}

	file = fopen("/tmp/sailboat/theta_wind", "w");
	if (file != NULL) { fprintf(file, "%.2f", theta_wind); fclose(file); }

}

void findAngle() 
{
//	bool inrange;
	float theta_LOS, theta_LOS0, theta_l, theta_r, theta_dl, theta_dr;
	float _Complex Xl, Xr, Xdl, Xdr;

	// DEADzone limit direction
	Xl = -sin(theta_nogo)*2.8284 + I*cos(theta_nogo)*2.8284;	//-2 + 2*1*I;
	Xr = sin(theta_nogo)*2.8284 + I*cos(theta_nogo)*2.8284;		// 2 + 2*1*I;        

	// DOWNzone limit direction
        Xdl = -sin(theta_down)*2.8284 - I*cos(theta_down)*2.8284;        //-2 - 2*1*I;
        Xdr = sin(theta_down)*2.8284 - I*cos(theta_down)*2.8284;        // 2 - 2*1*I;        

	// definition of angles
	theta_LOS = atan2(cimag(X_T_b)-cimag(X_b),creal(X_T_b)-creal(X_b));
	theta_LOS0 = atan2(cimag(X_T_b)-cimag(X0),creal(X_T_b)-creal(X0));
	theta_l = atan2(cimag(exp(-1*I*theta_LOS)*Xl),creal(cexp(-1*I*theta_LOS)*Xl));
	theta_r = atan2(cimag(exp(-1*I*theta_LOS)*Xr),creal(cexp(-1*I*theta_LOS)*Xr));
	// downwind angles
	theta_dl = atan2(cimag(exp(-1*I*theta_LOS)*Xdl),creal(cexp(-1*I*theta_LOS)*Xdl));
	theta_dr = atan2(cimag(exp(-1*I*theta_LOS)*Xdr),creal(cexp(-1*I*theta_LOS)*Xdr));

	// tacking boundaries
	// Line: x = a_x*y +/- b_x
	if (creal(X_T_b-X0) != 0) { a_x = creal(X_T_b-X0)/cimag(X_T_b-X0); }
	else {a_x=0;}


	//if (debug) printf("theta_LOS: %f \n",theta_LOS);
	//if (debug) printf("angle(Xdr): %f \n",atan2(cimag(Xdr),creal(Xdr)));
	//if (debug) printf("angle(Xdl): %f \n",atan2(cimag(Xdl),creal(Xdl)));

	//if (debug) printf("a_x: %f \n",a_x);
	b_x = TACKINGRANGE / (2 * sin(theta_LOS0));
	if (debug3) printf("\nTacking boundary end points:\n");
	if (debug3) printf("X0: %f + I*%f \n",creal(X0),cimag(X0));
	if (debug3) printf("X_T_b: %f + I*%f \n",creal(X_T_b),cimag(X_T_b));

	// Calculating tacking boundary points. X1 and X2 for left line, X3 and X4 right line.
	// y1 = ( creal(X_T)*(b_x+creal(X0))+cimag(X_T)*cimag(X0) )/( creal(X_T)*a_x+cimag(X_T) );
	X1 = 0 + I*( creal(X_T_b)*(b_x+creal(X0)) +cimag(X_T_b)*cimag(X0) )/( creal(X_T_b)*a_x+cimag(X_T_b) );
	X1 = a_x*cimag(X1)-b_x + I*cimag(X1);
	if (debug3) printf("X1: %f + I*%f \n",creal(X1),cimag(X1));

	X2 = 0 + I*( creal(X_T_b)*(b_x+creal(X_T_b)) +cimag(X_T_b)*cimag(X_T_b) )/( creal(X_T_b)*a_x+cimag(X_T_b) );
	X2 = a_x*cimag(X2)-b_x + I*cimag(X2);
	if (debug3) printf("X2: %f + I*%f \n",creal(X2),cimag(X2));

	X3 = 0 + I*( creal(X_T_b)*(-b_x+creal(X_T_b)) +cimag(X_T_b)*cimag(X_T_b) )/( creal(X_T_b)*a_x+cimag(X_T_b) );
	X3 = a_x*cimag(X3)+b_x + I*cimag(X3);
	if (debug3) printf("X3: %f + I*%f \n",creal(X3),cimag(X3));

	X4 = 0 + I*( creal(X_T_b)*(-b_x+creal(X0))+   cimag(X_T_b)*cimag(X0) )/( creal(X_T_b)*a_x+cimag(X_T_b) );
	X4 = a_x*cimag(X4)+b_x + I*cimag(X4);
	if (debug3) printf("X4: %f + I*%f \n",creal(X4),cimag(X4));


	// compute the next theta_d, ie at time t+1
	// (main algorithm)
		// Execution order:
		// 1. Is the LOS in deadzone?
		// 2. Is the LOS in downzone?
		// 3. the LOS is outside the zones, go straight.
		
		// LOS in dead zone
		if ( (atan2(cimag(Xr),creal(Xr))-PI/9)<=theta_LOS  &&  theta_LOS<=(atan2(cimag(Xl),creal(Xl))+PI/9) )
		{
			if (debug) printf("theta_d_b: %f \n",theta_d_b);
			//if (debug) printf("atan2(Xl): %f \n",atan2(cimag(Xl),creal(Xl)));
			//if (debug) printf("atan2(Xr): %f \n",atan2(cimag(Xr),creal(Xr)));

			if (theta_d_b >= atan2(cimag(Xl),creal(Xl))-PI/36  && theta_d_b <= atan2(cimag(Xl),creal(Xl))+PI/36 )
			{
				if (creal(X_b) < a_x*cimag(X_b)-b_x) { theta_d1_b = atan2(cimag(Xr),creal(Xr)); if (debug) printf(">> debug 3 \n"); fa_debug=3; sig1=1;}     
				else { theta_d1_b = theta_d_b; if (debug) printf(">> debug 4 \n"); fa_debug=4; sig1=0;}
			} 
			else
			{
				if (  (theta_d_b >= (atan2(cimag(Xr),creal(Xr))-(PI/36)))  &&  (theta_d_b <= (atan2(cimag(Xr),creal(Xr))+(PI/36))) )
				{
					if (creal(X_b) > a_x*cimag(X_b)+b_x) { theta_d1_b = atan2(cimag(Xl),creal(Xl)); if (debug) printf(">> debug 5 \n"); fa_debug=5; sig1=1;}
					else { theta_d1_b = theta_d_b; if (debug) printf(">> debug 6 \n"); fa_debug=6; sig1=0;}
				}
				else
				{
					if(cabs(theta_l) < cabs(theta_r)) { theta_d1_b = atan2(cimag(Xl),creal(Xl)); if (debug) printf(">> debug 7 \n"); fa_debug=7; sig1=1;}
					else { theta_d1_b = atan2(cimag(Xr),creal(Xr)); if (debug) printf(">> debug 8 \n"); fa_debug=8; sig1=1;}
				}
			}
		}
		else
		{
			// LOS in down zone
			if ( atan2(cimag(Xdr),creal(Xdr)) >= theta_LOS  &&  theta_LOS >= atan2(cimag(Xdl),creal(Xdl)) )
			{
                                //if (debug) printf("theta_d_b: %f \n",theta_d_b);
				//if (debug) printf("atan2 Xdl: %f \n",atan2(cimag(Xdl),creal(Xdl)));
				//if (debug) printf("atan2 Xdr: %f \n",atan2(cimag(Xdr),creal(Xdr)));

				if (theta_d_b >= atan2(cimag(Xdl),creal(Xdl))-PI/36  && theta_d_b <= atan2(cimag(Xdl),creal(Xdl))+PI/36 )
				{
					if (creal(X_b) > a_x*cimag(X_b)-b_x) { theta_d1_b = atan2(cimag(Xdr),creal(Xdr)); if (debug) printf(">> debug 13 \n"); fa_debug=13; sig1=1;}     
					else { theta_d1_b = theta_d_b; if (debug) printf(">> debug 14 \n"); fa_debug=14; sig1=0;}
				} 
				else
				{
					if (  (theta_d_b >= (atan2(cimag(Xdr),creal(Xdr))-(PI/36)))  &&  (theta_d_b <= (atan2(cimag(Xdr),creal(Xdr))+(PI/36))) )
					{
						if (creal(X_b) < a_x*cimag(X_b)+b_x) { theta_d1_b = atan2(cimag(Xdl),creal(Xdl)); if (debug) printf(">> debug 15 \n"); fa_debug=15; sig1=1;}
						else { theta_d1_b = theta_d_b; if (debug) printf(">> debug 16 \n"); fa_debug=16; sig1=0;}
					}
					else
					{
						if(cabs(theta_dl) < cabs(theta_dr)) { theta_d1_b = atan2(cimag(Xdl),creal(Xdl)); if (debug) printf(">> debug 17 \n"); fa_debug=17; sig1=1;}
						else { theta_d1_b = atan2(cimag(Xdr),creal(Xdr)); if (debug) printf(">> debug 18 \n"); fa_debug=18; sig1=1;}
						if (debug) printf("---- Downwind theta_d1_b: %f \n",theta_d1_b);
					}
				}
			}
			else
			{
				// if theta_LOS is outside of the deadzone
				theta_d1_b = theta_LOS;
				sig1 = 1;
				if (debug) printf(">> debug 2 \n");
				fa_debug=2;
			}
		}
	//if (debug) printf("theta_LOS = %f \n",theta_LOS);
	//if (debug) printf("X_T_b = %.1f + I*%.1f \n",creal(X_T_b),cimag(X_T_b));
	//if (debug) printf("X_b = %.1f + I*%.1f \n",creal(X_b),cimag(X_b));
	//if (debug) printf("Xl = %f + I*%f \n",creal(Xl),cimag(Xl));
	//if (debug) printf("Xr = %f + I*%f \n",creal(Xr),cimag(Xr));
	//if (debug) printf("Xdl = %f + I*%f \n",creal(Xdl),cimag(Xdl));
	//if (debug) printf("Xdr = %f + I*%f \n",creal(Xdr),cimag(Xdr));
}        

void chooseManeuver() 
{
	// The maneuver function does the maneuvers. This function performs each/every course change. 
	// Here it decides whether there is need for a tack, jibe or just a little course change. 
	// This decision incorporates two steps: 1. Is the desired heading on the other side of the deadzone? 
	// Then we need to jibe or tack. 2. Do we have enough speed for tacking? According to this it chooses sig.

	// float dAngle, d1Angle;
	float _Complex X_d_b, X_d1_b;

	// Definition of X_d1_b and X_d_b
	X_d_b = ccos(theta_d_b) + I*(csin(theta_d_b));
	X_d1_b = ccos(theta_d1_b) + I*(csin(theta_d1_b));

	if ( sign(creal(X_d_b)) == sign(creal(X_d1_b)) )
	{ sig2=0; }			//course change
	else
	{
		if ( cimag(X_d_b) > 0 && cimag(X_d1_b) > 0 && SOG > v_min )
		{ sig2=0; }	//tack
		else
		{		//jibe
			if ( creal(X_d_b) < 0 )
			{ sig2=1; }	//left
			else
			{ sig2=2; }	//right
		}
	}
	//if (debug) printf("X_d_b = %f + I*%f \n",creal(X_d_b),cimag(X_d_b));
	//if (debug) printf("X_d1_b = %f + I*%f \n",creal(X_d1_b),cimag(X_d1_b));
}

int sign(float val){
	if (val > 0) return 1;	// is greater then zero
	if (val < 0) return -1;	// is less then zero
	return 0;		// is zero
}



void performManeuver()
{
	// float v_b1, v_b2, v_d1_b1, v_d1_b2;
	float _Complex Xdl, Xdr;
	fa_debug=7353;

	//if (debug) printf("theta_b: %f\n",theta_b);
	//if (debug) printf("theta_d1_b: %f\n",theta_d1_b);
	if (sig2==1) if (debug) printf("Jibe left ... \n");
	if (sig2==2) if (debug) printf("Jibe right ... \n");

	// DOWNzone limit direction ***** These variables are already defined in the findAngle-function. ***
        Xdl = -sin(theta_down)*2.8284 - I*cos(theta_down)*2.8284;
        Xdr = sin(theta_down)*2.8284 - I*cos(theta_down)*2.8284;
	
	//Jibe direction -> jibe status -> defines the headings during the maneuver.
	switch(sig2)
	{
		case 1: // Jibe left
			switch(jibe_status)
			{
				case 1: //begin Jibe: get on course
					theta_pM_b = atan2(cimag(Xdl),creal(Xdl));
					jibe_pass_fcn();
					break;
				case 2: //tighten sail (hold course)
					theta_pM_b = atan2(cimag(Xdl),creal(Xdl));
					actIn = 1;
					jibe_pass_fcn();
					break;
				case 3: //perform jibe (hold sail tight)
					theta_pM_b = atan2(cimag(Xdr),creal(Xdr));
					actIn = 1;
					jibe_pass_fcn();
					break;
				case 4: //release sail (hold course)
					theta_pM_b = atan2(cimag(Xdr),creal(Xdr));
					actIn = 0;
					jibe_pass_fcn();
					break;
				case 5: //find new course
					jibe_pass_fcn();
					break;
			}
			break;
		case 2: // Jibe right
			switch(jibe_status)
			{
				case 1: //begin Jibe: get on course
					theta_pM_b = atan2(cimag(Xdr),creal(Xdr));
					jibe_pass_fcn();
					break;
				case 2: //tighten sail (hold course)
					theta_pM_b = atan2(cimag(Xdr),creal(Xdr));
					actIn = 1;
					jibe_pass_fcn();
					break;
				case 3: //perform jibe (hold sail tight)
					theta_pM_b = atan2(cimag(Xdl),creal(Xdl));
					actIn = 1;
					jibe_pass_fcn();
					break;
				case 4: //release sail (hold course)
					theta_pM_b = atan2(cimag(Xdl),creal(Xdl));
					actIn = 0;
					jibe_pass_fcn();
					break;
				case 5: //find new course
					jibe_pass_fcn();
					break;
			}
			break;
	} // end switch(sig2)
	//if (debug) printf("theta_pM_b = %f \n",theta_pM_b);
	if (debug_jibe) printf("jibe status = %d \n",jibe_status);
	if (debug_jibe) printf("actIn = %d \n",actIn);
	//if (debug) printf("Xdl = %f + I*%f \n",creal(Xdl),cimag(Xdl));
	//if (debug) printf("Xdr = %f + I*%f \n",creal(Xdr),cimag(Xdr));
}

/*	JIBE PASS FUNCTION
 *	increase the value of 'jibe_status' when needed and define sig3.
 */
void 	jibe_pass_fcn() {
	float _Complex X_h, X_pM;

	// defining direction unit vectors
	X_h = -sin(theta_b) + I*cos(theta_b);
	X_pM = -sin(theta_pM_b) + I*cos(theta_pM_b);

	//if (debug5) printf("jibe_pass_fcn: Sail_Feedback: %d\n",Sail_Feedback);

	if ( cos(angle_lim) < (creal(X_h)*creal(X_pM) + cimag(X_h)*cimag(X_pM)) && jibe_status<5) 
	{	// When the heading approaches the desired heading and the sail is tight, the jibe is performed.
		if ( actIn==0 && Sail_Feedback>300 ) jibe_status++;
		if ( actIn && Sail_Feedback<20 ) { jibe_status++; }
	}
	
	if ( jibe_status==5 ) {
		sig3=0;
		jibe_status=1; }
	else { sig3=sig2; }
	if (debug) printf("X_h = %f + I*%f \n",creal(X_h),cimag(X_h));
	if (debug) printf("X_pM = %f + I*%f \n",creal(X_pM),cimag(X_pM));
}




/*
 *	RUDDER PID CONTROLLER:
 *
 *	Calculate the desired RUDDER ANGLE position based on the Target Heading and Current Heading.
 *	The result is a rounded value of the angle stored in the [Rudder_Desired_Angle] global variable.
 *	Daniel Wrede, May 2013
 */
void rudder_pid_controller() {

	float dHeading, pValue, temp_ang, deshead; //,integralValue;
	
	switch (heading_state)
	{
		case 1:
			deshead = Guidance_Heading; 	// in degrees
			break;
		case 2:
			deshead = u_headsl;		// Steering after hill climbing controller
			break;
		case 3:
			deshead = u_head; 		// Steering after hill climbing controller
			break;
		case 4:
			deshead = headstep;		// Using the step heading algorithm
			break;
		case 5:
			deshead = des_heading;	// Heading straight in a direction
			break;
		case 6:
			deshead = theta_mean_wind + des_app_w;
			break;
		case 7:
			deshead = u_heel;
			break;
		default:
			deshead = Wind_Angle;		// Into the deadzone
			if (debug5) printf("heading_state switch case error.");
	}
	
	dHeading = deshead-Heading;
	deshead = acos(cos(deshead*PI/180))*180/PI;
	file = fopen("/tmp/sailboat/des_course", "w");
	if (file != NULL) { fprintf(file, "%d", (int)deshead); fclose(file); }

	// Singularity translation
	dHeading = dHeading*PI/180;
	dHeading = atan2(sin(dHeading),cos(dHeading));
	dHeading = dHeading*180/PI;        

	//if (debug) printf("dHeading: %f\n",dHeading);

	//if (debug) fprintf(stdout,"targetHeafing: %f, deltaHeading: %f\n",targetHeading, dHeading);
	//if (abs(dHeading) > dHEADING_MAX && abs(Rate) > RATEOFTURN_MAX) // Limit control statement
	//{

		// P controller
		pValue = GAIN_P * dHeading;

		// Integration part
		// The following checks, will keep integratorSum within -0.2 and 0.2
		// if (integratorSum < -INTEGRATOR_MAX && dHeading > 0) {
		// 	integratorSum = dHeading + integratorSum;
		// } else if (integratorSum > INTEGRATOR_MAX && dHeading < 0) {
		// 	integratorSum = dHeading + integratorSum;
		// } else {
		// 	integratorSum = integratorSum;
		// }
		// integralValue = GAIN_I * integratorSum;

		// result
		temp_ang = pValue; //+ integralValue; // Angle in radians

	//}
	// fprintf(stdout,"pValue: %f, integralValue: %f\n",pValue,integralValue);
	// fprintf(stdout,"Rudder_Desired_Angle: %d\n\n",Rudder_Desired_Angle);

	Rudder_Desired_Angle = round(temp_ang);
	if(Rudder_Desired_Angle > 35) {Rudder_Desired_Angle=35; }
	if(Rudder_Desired_Angle < -35) {Rudder_Desired_Angle=-35; }

	// Move rudder
	move_rudder(Rudder_Desired_Angle);
}



/*
 *	SAIL CONTROLLER (default)
 *
 *	Controls the angle of the sail and implements an Emergency sail release when the boat's
 *	roll value exceeds a predefined threshold. Input to this function is [Wind_Angle]
 *        Daniel Wrede & Mikkel Heeboell Callesen, December 2013
 */
void sail_controller() {

	float C=0, C_zero=0; 		// sheet lengths
	float BWA=0, theta_sail=0; 	// angle of wind according to heading, desired sail angle


	float _Complex X_h, X_w;

	X_h = csin(Heading*PI/180) + I*(ccos(Heading*PI/180));
	X_w = csin(Wind_Angle*PI/180) + I*(ccos(Wind_Angle*PI/180));
	BWA = acos( cimag(X_h)*cimag(X_w) + creal(X_h)*creal(X_w) );

	// Deriving the function for theta_sail:
	// theta_sail(BWA) = a*BWA+b
	// Having two points, theta_sail(theta_nogo)=0 and theta_sail(3/4*PI)=1.23
	// a=1.23/(3/4*PI-theta_nogo) , b=-a*theta_nogo
	// This is inserted below.

	if ( BWA < theta_nogo ) { theta_sail = 0; }
	else
	{
		if ( BWA < PI*3/4 ) { theta_sail = 1.23/( 3/4*PI-theta_nogo )*BWA - 1.23/(3/4*PI-theta_nogo)*theta_nogo; }
		else
		{
			if ( BWA < (PI*17/18) ) { theta_sail=1.23; }
			else { theta_sail = 0; }
		}
	}

	C = sqrt( SCLength*SCLength + BoomLength*BoomLength -2*SCLength*BoomLength*cos(theta_sail) + SCHeight*SCHeight);
	C_zero = sqrt( SCLength*SCLength + BoomLength*BoomLength -2*SCLength*BoomLength*cos(0) + SCHeight*SCHeight);

	// Assuming the actuator to be out at ACT_MAX and in at 0:
	//(C-C_zero)=0 -> sail tight when C=C_zero . ACT_MAX/500 is the ratio between ticks and stroke. 1000[mm]/3 is a unit change + 
	Sail_Desired_Position = round( (C-C_zero)/3*ACT_MAX/strokelength ); 
	if ( Sail_Desired_Position > ACT_MAX ) Sail_Desired_Position=ACT_MAX; 
	if ( Sail_Desired_Position < 0 )   Sail_Desired_Position=0; 

	// If the boat tilts too much
	if(fabs(Roll)>ROLL_LIMIT) // we multiply by 3.26, to transform the input to degrees.
	{ 
                // Start Loosening the sail
		desACTpos = ACT_MAX;      
		roll_counter = 0;
		// if (debug) printf("max roll reached. \n" );
	} 
	else
	{
		if(roll_counter<10*SEC) { roll_counter++; }
	}

	// if (debug) printf("sail_controller readout: SIG3 = %d \n",sig3);
	// If it is time to jibe
	if (actIn) 
	{
		// Start Tightening the sail
		desACTpos = 0;
	}
	else
	{
		// sail tuning according to wind
		if(roll_counter > 5*SEC && (fabs(Sail_Desired_Position-Sail_Feedback)>SAIL_LIMIT || tune_counter>10*SEC) )
		{
			desACTpos = Sail_Desired_Position;
			tune_counter=0;
			//if (debug2) printf("- - - Sail tuning - - -\n");
		}
		else
		{
		tune_counter++;
		}
	}
	//if (debug2) printf("desACTpos: %d \n",desACTpos);
	//if (debug2) printf("Sail_Feedback: %d \n",Sail_Feedback);
}

/*
 *		Sign function finding pos or neg value
 */
int signfcn(float in)
{	
	//if (debug5) printf("sgnfcn in = %f \n", in);
	int out;
	if (in >= 0) { out = 1; }
	else { out = -1; }
	//if (debug5) printf("sgnfcn out = %d \n", out);
	return out;
}

/*
 *		Global count function, to synchronize hill climbing functions
 *
 */
void countFCN()
{
	if (counter >= steptime*SEC-1) counter=0;
	else counter++;
	if (debug6) printf("global counter : %d \n", counter);
}

/*
 *	SAIL CONTROLLER (based on hillclimbing function) [from "thesis" branch]
 *
 *	Actuates the sail in one direction for [SAIL_ACT_TIME] seconds
 *	Calculate the mean velocity of the boat on a period of [SAIL_OBS_TIME] seconds
 *	If the velocity is increasing keep moving in the same direction, otherwise change direction
 */


void sail_hc_controller() {

	int news;
	float dv, du;
	int signv, signu;
	int k_sail=5;			// stepsize in degrees //
	k_sail = sail_stepsize;
	static float v_mean, v_sail, v_old_sail=20, u_old_sail=13;
	static int intern_sail_pos;
	int Heading_des = 0;
	Heading_des = vLOS;
	static float V_vmg[120];
	int m = steptime/2*SEC, n=0;
	static int a=0;
	
// CALCULATE MEAN VELOCITY by: using a circular buffer to store the velocities
	if (a > m-1) a=0;
	if (sail_state == 2 && (heading_state == 3 || heading_state == 6) )
	{
		if (Simulation) V_vmg[a] = v_poly*cosf((Heading-Heading_des)*PI/180);
		else V_vmg[a] = SOG*cosf((Heading-Heading_des)*PI/180);
		
		//if (debug) printf("Combined run. \n");
	}
	else
	{
		if (Simulation) V_vmg[a] = v_poly;
		else V_vmg[a] = SOG;
	}
	a++;
	if (debug) printf("Velocity Made Good		vmg: %f [m/s] \n", v_poly*cosf((Heading-Heading_des)*PI/180));
	// and taking the mean of the velocity vector
	if (counter == steptime*SEC/2 - 1 || counter == 0)
	{
		v_mean=0;
		for ( n=0; n<m; n++) v_mean = v_mean + V_vmg[n];
		v_mean = v_mean/(m);			// mean velocity value
		if (debug) printf("MEAN CALC	v_mean: %f [m/s] \n", v_mean);
	}
	
//  ****************************************************************************

	
// GETTING THE CORRECT CONTROL INPUT
	if (sail_state == 2 && heading_state == 3 && counter == 0)
	{	
		v_sail = v_mean;
		
		if (debug) printf("Update Sail. 	v_sail=%f \n", v_sail);
	}
	else{if ( heading_state != 3 )
	{
		if (Simulation) v_sail = v_poly;
		else v_sail = SOG;

		//if (debug) printf("Single mode SAIL. \n");
	}}
	
//  ****************************************************************************
		
		
// USING HILL-CLIMBING METHOD
	if (counter == steptime*SEC/2 - 1)
	{
		dv = v_sail-v_old_sail;
		//if (dv >= 0) signv=1;
		//else signv=-1;
		if (debug) printf("Control: 	v_sail=%f \n", v_sail);
		signv = signfcn(dv);

		du = u_sail-u_old_sail;
		//if (du >= 0) signu=1;
		//else signu=-1;
		signu = signfcn(du);

		news = signv*signu;
		ctri_sail = v_sail;		// global variable 'control input', saved.
		v_old_sail = v_sail;
		u_old_sail = u_sail;
		u_sail = u_sail + k_sail*news;
		if (u_sail < 0) u_sail=u_old_sail + k_sail;
		if (u_sail > 90) u_sail=u_old_sail - k_sail;
		desACTpos = ACT_MAX/strokelength/2*(sqrt( SCLength*SCLength + BoomLength*BoomLength -2*SCLength*BoomLength*cos(u_sail*PI/180) + SCHeight*SCHeight)-sqrt( SCLength*SCLength + BoomLength*BoomLength -2*SCLength*BoomLength*cos(0) + SCHeight*SCHeight));
		
		file = fopen("/tmp/sailboat/u_sail", "w");
		if (file != NULL) { fprintf(file, "%d", (int)u_sail); fclose(file); }
	}
	
//  ****************************************************************************

	
	if (sail_pos != intern_sail_pos) {
		intern_sail_pos = sail_pos; 	// When the input changes, all variables are updated
		u_sail = sail_pos; 		// to the new input value. Here intern_sail_pos is used
		desACTpos = ACT_MAX/strokelength/2*(sqrt( SCLength*SCLength + BoomLength*BoomLength -2*SCLength*BoomLength*cos(sail_pos*PI/180) + SCHeight*SCHeight)-sqrt( SCLength*SCLength + BoomLength*BoomLength -2*SCLength*BoomLength*cos(0) + SCHeight*SCHeight));				// to track input changes.
		}
	
	/*if(debug_hc && counter_sail==0) printf("---- Sail Hill Climbing ----\n");
	if(debug_hc && counter_sail==0) printf("u_sail: %d \n",u_sail);
	if(debug_hc && counter_sail==0) printf("Sail_Feedback: %d \n",Sail_Feedback);
	if(debug_hc && counter_sail==0) printf("v_poly: %f \n",v_poly);
	if(debug_hc && counter_sail==0) printf("signv: %d \n",signv);
	if(debug_hc && counter_sail==0) printf("signu: %d \n",signu); */
}




/*
 * Heading Hill Climbing COS Controller
 *
 * Changes the heading to reach an increased velocity
 * in the desired direction.
 */

void heading_hc_controller() 
{	
	int news;
	float dv, du, v_mean=0;
	static float v_head, v_old_head=20;
	int signv, signu;
	int k_head = 10;            		// angular steps in degrees
	int Heading_des = 0;			// degrees // desired heading
	static int u_old_head=13, intern_DIR_init;
	static float V_vmg[120];
	int m = steptime/2*SEC, n=0;
	static int a=0;
	
	k_head = stepsize;
	Heading_des = vLOS;
	
// CALCULATE MEAN VELOCITY by: using a circular buffer to store the velocities
	if (a > m-1) a=0;
	if (Simulation) V_vmg[a] = v_poly*cosf((Heading-Heading_des)*PI/180);
	else V_vmg[a] = SOG*cosf((Heading-Heading_des)*PI/180);
	a++;
	if (debug) printf("Velocity Made Good				vmg: %f [m/s] \n", v_poly*cosf((Heading-Heading_des)*PI/180));
	// and taking the mean of the velocity vector
	if (counter == steptime*SEC/2 - 1 || counter == 0)
	{
		v_mean=0;
		for ( n=0; n<m; n++) v_mean = v_mean + V_vmg[n];
		v_mean = v_mean/(m);			// mean velocity value
		if (debug) printf("MEAN CALC				v_mean: %f [m/s] \n", v_mean);
	}
	
//  ****************************************************************************


	
// GETTING THE CORRECT CONTROL INPUT
	if (sail_state == 2 && heading_state == 3 && counter == steptime*SEC/2 - 1)
	{	
		v_head = v_mean;
		
		if (debug) printf("Update VMG Heading. 			v_head=%f \n", v_head);
	}
	else{if ( sail_state != 2 && counter == 0)
	{
		//if (debug) printf("Single mode VMG. \n");
		v_head = v_mean;
	}}
	
//  ****************************************************************************
	
// USING HILL-CLIMBING METHOD
	if (counter == 0) {
		//if (debug5) printf("2 We're alright \n");
		dv = v_head-v_old_head;
		if (debug) printf("Control: 				v_head=%f \n", v_head);
		signv = signfcn(dv);

		du = u_head-u_old_head;
		signu = signfcn(du);

		news = signv*signu;
		ctri_head = v_head;		// global variable 'control input', saved.
		v_old_head = v_head;
		u_old_head = u_head;
		u_head = u_head + k_head*news;
	}

//  ****************************************************************************
	
	if (intern_DIR_init != DIR_init) {
		if (debug5) printf("DIR_init=%d, u_head=%d \n", DIR_init, u_head);
		intern_DIR_init = DIR_init;
		u_head = DIR_init;
		}
	
	if(debug_hc && counter==0) printf("Heading: %f \n",Heading);
	if(debug_hc && counter==0) printf("v_poly: %f \n",v_poly);
}


/*
 * Heading Hill Climbing Slope Controller
 *
 * Changes the heading to reach a velocity slope (wrt heading)
 * defining the upwind edge.
 */

void heading_hc_slope_controller() 
{	
	int news;
	float du, v_headsl, inthesign;
	int signu, k_headsl=10;               	// angular steps in degrees
	static float v_old_headsl = 20;
	static int u_old_headsl = 13, intern_DIR_init;
	
	if (Simulation) v_headsl = v_poly;
	else v_headsl = SOG;
	k_headsl=stepsize;

	if (counter == 0)
	{ // Sequence: Calculate the value "news". Update "old" variables. Change u_headsl.
		//if (debug5) printf("**** Printsession Start **** \n");
		du = u_old_headsl-u_headsl;
		//if (debug5) printf("du = %f \n", du);
		signu = signfcn(du);
		//if (debug5) printf("signu = %d \n", signu);

				
		inthesign = signu*(v_old_headsl-v_headsl)/k_headsl - des_slope;
		//if (debug5) printf("des_slope: %f \n", des_slope);
		//if (debug5) printf("Results in slope estimate: %f \n", signu*(v_old_headsl-v_headsl)/k_headsl);
		//if (debug5) printf("inthesign: %f \n", inthesign);
		news = signfcn(inthesign);
		//if (debug5) printf("news = %d \n", news);
		
		ctri_headsl = v_headsl;		// global variable 'control input', saved.
		v_old_headsl = v_headsl;
		u_old_headsl = u_headsl;
			
		u_headsl = u_headsl + k_headsl*news;
		
		//if (debug5) printf("v_old_headsl = %.2f \n", v_old_headsl);
		//if (debug5) printf("v_headsl = %.2f \n", v_headsl);
		//if (debug5) printf("u_old_headsl = %d \n", u_old_headsl);
		//if (debug5) printf("u_headsl = %d \n", u_headsl);
		//if (debug5) printf("**** Printsession End **** \n");
	}
	//if (debug5) printf("counter_headsl = %d \n", counter_headsl);
	if (intern_DIR_init != DIR_init) {
		if (debug5) printf("DIR_init=%d, u_headsl=%d \n", DIR_init, u_headsl);
		intern_DIR_init = DIR_init;
		u_headsl = DIR_init; }
}

/*
 *		The following algorithm uses the heeling to track the upwind course,
 *		by assuming to sail upwind, when max heeling is reached.
 */
 
void heading_hc_heeling_controller()
{	
	int signh, signu;
	float dh, du, heeling;
	static int u_old=13, intern_DIR_init;
	static float heel_old, V_heeling[40];
	int meantime = 5;			// [sec] Duration for mean calculation
	int m = meantime*SEC, n=0;
	static int a=0;

	if (Simulation) heeling = heel_sim;
	else heeling = Roll;
	heeling = fabs(heeling);		// ensure the value being positive always
	
	// Using a circular buffer to store the wind angles
	if (a > m-1) a=0;
	V_heeling[a] = heeling;
	a++;
	
	// taking the mean of the heeling vector
	heeling=0;
	for ( n=0; n<m; n++) heeling = heeling + V_heeling[n];
	heeling = heeling/m;			// mean heeling value
	if (debug6) printf("mean heeling: %f [rad] \n", heeling);
	
	if (counter == 0)
	{

		dh = heeling - heel_old;
		signh = signfcn(dh);
		du = u_heel-u_old;
		signu = signfcn(du);

		ctri_heel = heeling;		// global variable 'control input', saved.
		heel_old = heeling;
		u_old = u_heel;
		u_heel = u_heel + stepsize*signh*signu;
		
		if (debug6) printf("Heeling chosen heading: %d [deg] \n", u_heel);
	}

	if (intern_DIR_init != DIR_init) {
		if (debug5) printf("DIR_init=%d, u_heel=%d \n", DIR_init, u_heel);
		intern_DIR_init = DIR_init;
		u_heel = DIR_init; }
}


/*
 *		The stepheading function changes the heading in time steps.
 *			Hence sireceiving well developed data for plots.
 */

void stepheading()
{	
	static int counter_stephead=0, steps=0, intern_DIR_init;
	int dirsteps=1;
	int apparent[23]= {180, 180, 160, 140, 120, 100, 90, 80, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20, 15, 10, 5, 0};
	if (stepDIR >= 0) dirsteps = -1;
	else dirsteps = 1;

	if (counter_stephead >= steptime*SEC) { counter_stephead = 0; steps++; if (debug6) printf("---\n Heading: %f \n v_poly = %f \n", headstep-theta_mean_wind, v_poly);}
	else counter_stephead++;

	if (intern_DIR_init != DIR_init) {
		intern_DIR_init = DIR_init;
		steps = 0; }
		
	if (steps >= 22) { steps=0; if (debug5) printf("Task Completed\n"); }
	//if (debug5) printf("Counter_stephead: %d \n", counter_stephead);
	headstep = theta_mean_wind + dirsteps*apparent[steps];
}

/*
 *	Mean wind function, finding the mean wind. 
 */

void meanwind() {
	int meantime = 10;			// [sec] Duration for mean wind direction
	int m = meantime*SEC, n=0;
	static int a=0;
	float _Complex V_WIND=0;
	static float V_angles[40];		//[m+1]; static float?

	//for ( n=0; n<m; n++) V_angles[m-n] = V_angles[m-n-1];
	
	// Using a circular buffer to store the wind angles
	if (a > m-1) a=0;
	V_angles[a] = Wind_Angle;
	//if (debug5) printf("V_angles[%d]=%f \n", a, V_angles[a]);
	a++;
	
	// summing up a wind vector, containing all stored directions
	for ( n=0; n<=m; n++) V_WIND = V_WIND + ( sinf(V_angles[n]*PI/180) + I*cosf(V_angles[n]*PI/180) );
	theta_mean_wind = round( atan2( creal(V_WIND) , cimag(V_WIND) )*180/PI );
	//if (debug5) printf("theta_mean_wind = %f \n", theta_mean_wind);
	
	file = fopen("/tmp/sailboat/mean_wind", "w");
	if (file != NULL) { fprintf(file, "%d", (int)theta_mean_wind); fclose(file); }
	
	//if (debug6) printf("theta mean wind: %f \n",theta_mean_wind);
}



void simulate_sailing() {
	
	// update boat heading
	double delta_Heading = (SIM_ROT/SEC)*(-(double)Rudder_Desired_Angle/30)*SIM_SOG;
	Heading = Heading + delta_Heading;

	// update sail actuator position
	int increment=SIM_ACT_INC/SEC;
	int desACTpos_sim=0;

	file = fopen("/tmp/sailboat/Navigation_System_Sail", "r");
	if (file != NULL) { fscanf(file, "%d", &desACTpos_sim); fclose(file); }

	if (Sail_Feedback > desACTpos_sim) Sail_Feedback-=increment; 
	else {if(Sail_Feedback < desACTpos_sim)	{ Sail_Feedback+=increment; }}

	// calculate apparent wind
	float app_wind = (Heading-Wind_Angle)*PI/180;	// apparent wind direction
	app_wind = atan2(sin(app_wind),cos(app_wind));	// avoiding singularities
	if (debug5) printf("2 app_wind = %f \n", app_wind*180/PI);	// printing to check
	app_wind = fabs(app_wind);
	//if ( app_wind < 0 ) app_wind = 2*PI+app_wind;	// putting on a scale from 0 to 2*PI
	if (debug5) printf("3 app_wind = %f \n", app_wind*180/PI);	// printing to check
	
	// calculate boat velocity
	if ( app_wind > 0.22 && app_wind < PI ) {
		v_poly = (-0.0147*power(app_wind,6) + 0.2772*power(app_wind,5) - 2.1294*power(app_wind,4) + 8.5197*power(app_wind,3) - 18.464*power(app_wind,2) + 19.847*app_wind - 3.4774)*1.6/4.7;
		v_poly = v_poly*10;
		}
	else { 	/*if ( app_wind > PI && app_wind < 6.06 ) {
			v_poly = (-0.0147*power((2*3.1121-app_wind),6) + 0.2772*power((2*3.1121-app_wind),5) - 2.1294*power((2*3.1121-app_wind),4) + 8.5197*power((2*3.1121-app_wind),3) - 18.464*power((2*3.1121-app_wind),2) + 19.847*(2*3.1121-app_wind) - 3.4774)*1.6/4.7;
			} else {*/
		v_poly=0;
		}
	
	/*if ( app_wind > 0.22 && app_wind < 6.09 ) {
		v_poly = (-0.0147*power(app_wind,6) + 0.2772*power(app_wind,5) - 2.1294*power(app_wind,4) + 8.5197*power(app_wind,3) - 18.464*power(app_wind,2) + 19.847*app_wind - 3.4774)*1.6/4.7;
		}
	else {v_poly=0;} */

	// Adding sail position dependence (which isn't totally correct, but makes things work ;-] )
	v_poly = v_poly - 0.000005285*power(Sail_Feedback,2) + 0.0045983*Sail_Feedback;
	if (debug5) printf("v_poly = %f \n", v_poly);	// printing to check
	//v_poly = SIM_SOG;
	
	// Calculate heeling angle
	heel_sim = 0.0588*power(app_wind,6)-0.6542*power(app_wind,5)+2.8009*power(app_wind,4)-5.6456*power(app_wind,3)+5.0785*power(app_wind,2)-1.3435*app_wind+0.1104;
	if (debug5) printf("heel_sim : %f \n", heel_sim);
		
	// update boat position
	double displacement = ((double)v_poly)/SEC;
	double SimLon= ( (Longitude*CONVLON) + displacement*sinf(Heading*PI/180) )/CONVLON;
	double SimLat= ( (Latitude*CONVLAT)  + displacement*cosf(Heading*PI/180) )/CONVLAT;
	Latitude=(float)SimLat;
	Longitude=(float)SimLon;

	if(debug2) printf("Sail_Feedback_sim: %d \n",Sail_Feedback);
	if(debug_hc) printf("v_poly: %f \n",v_poly);


	// update rudder position (for the GUI only)
	increment=16/SEC;	// 16 degrees/sec
	if (Rudder_Feedback > Rudder_Desired_Angle) Rudder_Feedback-=increment; 
	else Rudder_Feedback+=increment; 


	// change wind conditions
	// not implemented

	
	// debug
	// if (debug) printf("FA_DEBUG:[%d]\n",fa_debug);


	// Write new values to file
	file = fopen("/tmp/u200/Heading", "w");
	if (file != NULL) { fprintf(file, "%f", Heading); fclose(file); }
	file = fopen("/tmp/u200/Latitude", "w");
	if (file != NULL) { fprintf(file, "%.8f", Latitude); fclose(file); }
	file = fopen("/tmp/u200/Longitude", "w");
	if (file != NULL) { fprintf(file, "%.8f", Longitude); fclose(file); }
	file = fopen("/tmp/sailboat/Sail_Feedback", "w");
	if (file != NULL) { fprintf(file, "%d", Sail_Feedback); fclose(file); }
	file = fopen("/tmp/sailboat/Rudder_Feedback", "w");
	if (file != NULL) { fprintf(file, "%d", Rudder_Feedback); fclose(file); }
}

float power(float number, float eksponent) {
	int n;
	float output=1;
	for (n=0; n<eksponent; n++) output = output*number;
	return output;	
	}

/*
 *	Read data from the Weather Station
 */
void read_weather_station() {

	//RATE OF TURN
	file = fopen("/tmp/u200/Rate", "r");
	if (file != NULL) {
		fscanf(file, "%f", &Rate);
		fclose(file);
	} else {
		printf("ERROR: Files from Weather Station are missing.\n");
		exit(1);
	}

	//VESSEL HEADING
	file = fopen("/tmp/u200/Heading", "r");
	if (file != NULL) { 
		fscanf(file, "%f", &Heading); fclose(file);
	}
/*	file = fopen("/tmp/u200/Deviation", "r");
	if (file != NULL) {
		fscanf(file, "%f", &Deviation);	fclose(file);
	}
	file = fopen("/tmp/u200/Variation", "r");
	if (file != NULL) {
		fscanf(file, "%f", &Variation);	fclose(file);
	}
*/
	//ATTITUDE
/*	file = fopen("/tmp/u200/Yaw", "r");
	if (file != NULL) {
		fscanf(file, "%f", &Yaw); fclose(file);
	}
*/	file = fopen("/tmp/u200/Pitch", "r");
	if (file != NULL) {
		fscanf(file, "%f", &Pitch);	fclose(file);
	}
	file = fopen("/tmp/u200/Roll", "r");
	if (file != NULL) {
		fscanf(file, "%f", &Roll);	fclose(file);
		Roll = Roll*3.26;
	}

	//GPS_DATA
	file = fopen("/tmp/u200/Latitude", "r");
	if (file != NULL) {
		fscanf(file, "%f", &Latitude); fclose(file);
	}
	file = fopen("/tmp/u200/Longitude", "r");
	if (file != NULL) {
		fscanf(file, "%f", &Longitude);	fclose(file);
	}
	file = fopen("/tmp/u200/COG", "r");
	if (file != NULL) {
		fscanf(file, "%f", &COG); fclose(file);
	}
	file = fopen("/tmp/u200/SOG", "r");
	if (file != NULL) {
		fscanf(file, "%f", &SOG); fclose(file);
	}
	//WIND_DATA
	file = fopen("/tmp/u200/Wind_Speed", "r");
	if (file != NULL) {
		fscanf(file, "%f", &Wind_Speed); fclose(file);
	}
	file = fopen("/tmp/u200/Wind_Angle", "r");
	if (file != NULL) {
		fscanf(file, "%f", &Wind_Angle); fclose(file);
	}

}


/*
 *	Read essential data from the Weather Station
 */
void read_weather_station_essential() {

	//VESSEL HEADING
	file = fopen("/tmp/u200/Heading", "r");
	if (file != NULL) { 
		fscanf(file, "%f", &Heading); fclose(file);
	}
	//GPS_DATA
	file = fopen("/tmp/u200/Latitude", "r");
	if (file != NULL) {
		fscanf(file, "%f", &Latitude); fclose(file);
	}
	file = fopen("/tmp/u200/Longitude", "r");
	if (file != NULL) {
		fscanf(file, "%f", &Longitude);	fclose(file);
	}
	//WIND_DATA
	file = fopen("/tmp/u200/Wind_Speed", "r");
	if (file != NULL) {
		fscanf(file, "%f", &Wind_Speed); fclose(file);
	}
	file = fopen("/tmp/u200/Wind_Angle", "r");
	if (file != NULL) {
		fscanf(file, "%f", &Wind_Angle); fclose(file);
	}

}


/*
 *	Read target point coordinates from files
 *	If the target point is changed on the fly, the start point is
 *	updated with the current position
 */
void read_target_point() {

	float prev_Lat, prev_Lon;
	prev_Lat=Point_End_Lat;
	prev_Lon=Point_End_Lon;
	

	file = fopen("/tmp/sailboat/Point_End_Lat", "r");
	if (file != NULL) {
		fscanf(file, "%f", &Point_End_Lat); fclose(file);
	}
	file = fopen("/tmp/sailboat/Point_End_Lon", "r");
	if (file != NULL) {
		fscanf(file, "%f", &Point_End_Lon);	fclose(file);
	}

	// if target point has changed, update Starting point
	if ((prev_Lat!=Point_End_Lat) || (prev_Lon!=Point_End_Lon)) {
		Point_Start_Lat=Latitude;
		Point_Start_Lon=Longitude;
		file = fopen("/tmp/sailboat/Point_Start_Lat", "w");
		if (file != NULL) { fprintf(file, "%f", Point_Start_Lat); fclose(file); }
		file = fopen("/tmp/sailboat/Point_Start_Lon", "w");
		if (file != NULL) { fprintf(file, "%f", Point_Start_Lon); fclose(file); }
	}

}


/*
 *	Read external_variables
 */
void read_external_variables() {

	// declare temporary variables
	int tmp_sail_state, tmp_heading_state, tmp_steptime, tmp_stepsize;
	float tmp_des_slope;
	int tmp_vLOS, tmp_DIR, tmp_DIR_init, tmp_des_heading, tmp_sail_stepsize, tmp_sail_pos, tmp_des_app_w;
	
	static int ext_heading_state, ext_sail_state, ext_steptime, ext_stepsize;
	static float ext_des_slope;
	static int ext_vLOS, ext_DIR, ext_DIR_init, ext_des_heading, ext_sail_stepsize, ext_sail_pos, ext_des_app_w;

	
	// assign values to temporary values
	tmp_sail_state = ext_sail_state;
	tmp_heading_state = ext_heading_state;
	tmp_steptime = ext_steptime;
	tmp_stepsize = ext_stepsize;
	tmp_des_slope = ext_des_slope;
	tmp_vLOS = ext_vLOS;
	tmp_DIR = ext_DIR;
	tmp_DIR_init = ext_DIR_init;
	tmp_des_heading = ext_des_heading;
	tmp_des_app_w = ext_des_app_w;
	tmp_sail_stepsize = ext_sail_stepsize;
	tmp_sail_pos = ext_sail_pos;

	// read from files
	file = fopen("/tmp/sailboat/ext_sail_state", "r");
	if (file != NULL) { fscanf(file, "%d", &ext_sail_state); fclose(file); }
	file = fopen("/tmp/sailboat/ext_heading_state", "r");
	if (file != NULL) { fscanf(file, "%d", &ext_heading_state); fclose(file); }
	file = fopen("/tmp/sailboat/ext_steptime", "r");
	if (file != NULL) { fscanf(file, "%d", &ext_steptime); fclose(file); }
	file = fopen("/tmp/sailboat/ext_stepsize", "r");
	if (file != NULL) { fscanf(file, "%d", &ext_stepsize); fclose(file); }
	file = fopen("/tmp/sailboat/ext_des_slope", "r");
	if (file != NULL) { fscanf(file, "%f", &ext_des_slope); fclose(file); }
	file = fopen("/tmp/sailboat/ext_vLOS", "r");
	if (file != NULL) { fscanf(file, "%d", &ext_vLOS); fclose(file); }
	file = fopen("/tmp/sailboat/ext_DIR", "r");
	if (file != NULL) { fscanf(file, "%d", &ext_DIR); fclose(file); }
	file = fopen("/tmp/sailboat/ext_DIR_init", "r");
	if (file != NULL) { fscanf(file, "%d", &ext_DIR_init); fclose(file); }
	file = fopen("/tmp/sailboat/ext_des_heading", "r");
	if (file != NULL) { fscanf(file, "%d", &ext_des_heading); fclose(file); }
	file = fopen("/tmp/sailboat/ext_des_app_w", "r");
	if (file != NULL) { fscanf(file, "%d", &ext_des_app_w); fclose(file); }
	file = fopen("/tmp/sailboat/ext_sail_stepsize", "r");
	if (file != NULL) { fscanf(file, "%d", &ext_sail_stepsize); fclose(file); }
	file = fopen("/tmp/sailboat/ext_sail_pos", "r");
	if (file != NULL) { fscanf(file, "%d", &ext_sail_pos); fclose(file); }


	
	// update variables in the algorithm only when something changes in files
	if (tmp_sail_state != ext_sail_state) {	
		sail_state = ext_sail_state; 
		if(debug5) printf("current sail state: %d \n", ext_sail_state); }

	if (tmp_heading_state != ext_heading_state) {
		heading_state = ext_heading_state; 
		if(debug5) printf("current heading state: %d \n", ext_heading_state); }

	if (tmp_steptime != ext_steptime) {	
		steptime = ext_steptime;
		if(debug5) printf("current steptime: %d \n", ext_steptime); }

	if (tmp_stepsize != ext_stepsize) {	
		stepsize = ext_stepsize; 
		if(debug5) printf("current stepsize: %d \n", ext_stepsize); }

	if (tmp_des_slope != ext_des_slope) {	
		des_slope = ext_des_slope; 
		if(debug5) printf("current des_slope: %f \n", ext_des_slope); }
	if (tmp_vLOS != ext_vLOS) {	
		vLOS = ext_vLOS; 
		if(debug5) printf("current vLOS: %d \n", ext_vLOS); }
	if (tmp_DIR != ext_DIR) {	
		stepDIR = ext_DIR; 
		if(debug5) printf("current DIR: %d \n", ext_DIR); }
	
	if (tmp_DIR_init != ext_DIR_init) {	
		DIR_init = ext_DIR_init; 
		if(debug5) printf("current DIR_init: %d \n", ext_DIR_init); }
			
	if (tmp_des_heading != ext_des_heading) {	
		des_heading = ext_des_heading; 
		if(debug5) printf("current des_heading: %d \n", ext_des_heading); }
	if (tmp_des_app_w != ext_des_app_w) {	
		des_app_w = ext_des_app_w; 
		if(debug5) printf("current des_app_w: %d \n", ext_des_app_w); }
	if (tmp_sail_stepsize != ext_sail_stepsize) {	
		sail_stepsize = ext_sail_stepsize; 
		if(debug5) printf("current sail_stepsize: %d \n", ext_sail_stepsize); }
	if (tmp_sail_pos != ext_sail_pos) {	
		sail_pos = ext_sail_pos; 
		if(debug5) printf("current sail_pos: %d \n", ext_sail_pos); }
}

/*
 *	Move the rudder to the desired position.
 *	Write the desired angle to a file [Navigation_System_Rudder] to be handled by another process 
 */
void move_rudder(int angle) {
	file = fopen("/tmp/sailboat/Navigation_System_Rudder", "w");
	if (file != NULL) { fprintf(file, "%d", angle);	fclose(file); }
}

/*
 *	Move the main sail to the desired position.
 *	Write the desired angle to a file [Navigation_System_Sail] to be handled by another process 
 */
void move_sail(int position) {
	
	// Duty cycle observer
	int n, i, dutysum=0, m=60; 	//dtime*SEC;
	static int act_history[60]={0}, actStop = 0;
	float mm=m;

	float duty=0;
	for (n=0; n<m; n++) act_history[m-n] = act_history[m-n-1];

	if ( abs(Sail_Feedback-position) > ACT_PRECISION && actStop==0) { act_history[0] = 1;}
	else { act_history[0] = 0;}
	
	for (i=0; i<m; i++) {dutysum = dutysum + act_history[i];}
	duty = dutysum / mm;	
	if (duty > MAX_DUTY_CYCLE) actStop=1;
	else{if (duty <= 0.25) {   actStop=0; } } // When a low duty cycle % is reached, the actStop is reset.

	if (actStop==0)
	{
		file = fopen("/tmp/sailboat/Navigation_System_Sail", "w");
		if (file != NULL) { fprintf(file, "%d", position); fclose(file);}// Sail_Desired_Position=position;}
		if(debug2) printf("move_sail: desACTpos = %d \n", position);
	}
	

	if (debug4) printf("Sail_Feedback - position (of the sail)= abs: %d - %d = %d \n",Sail_Feedback, position, Sail_Feedback-position);	
	//if (debug4) printf("dutysum: %d \n",dutysum);
	if (debug4) printf("duty: %f \n",duty);
	if (debug4) printf("actStop: %d \n",actStop);
	if (debug4) printf("** end move sail");

	// Write "duty" to file
	file = fopen("/tmp/sailboat/duty", "w");
	if (file != NULL) { fprintf(file, "%.2f", duty); fclose(file);}
}



/*
 *	Read Sail actuator feedback
 */
void read_sail_position() {

	file = fopen("/tmp/sailboat/Sail_Feedback", "r");
	if (file != NULL) { 
		fscanf(file, "%d", &Sail_Feedback); fclose(file);
	}
}



/*
 *	Save all the variables of the navigation system in a log file in sailboat-log/
 *	Create a new log file every MAXLOGLINES rows
 */
void write_log_file() {

	FILE* file2;
	DIR * dirp;
	char  logline[1000];
	char  timestp[25];

	time_t rawtime;
	struct tm  *timeinfo;
	struct dirent * entry;

	// create a new file every MAXLOGLINES
	if(logEntry==0 || logEntry>=MAXLOGLINES) {

		// pin pointer
		file2 = fopen("sailboat-log/current_logfile", "w");
		if (file2 != NULL) { fprintf(file2, "init"); fclose(file2); }
				
		//count files in log folder
		int file_count = 0;
		dirp = opendir("sailboat-log/"); 

		while ((entry = readdir(dirp)) != NULL) {
			if (entry->d_type == DT_REG) { 
				 file_count++;
			}
		}
		closedir(dirp);

		// calculate new timestamp
		time(&rawtime);
		timeinfo = localtime(&rawtime);
		strftime(timestp, sizeof timestp, "%Y%m%d_%H%M", timeinfo);
		
		// save log filename for other processes
		file2 = fopen("sailboat-log/current_logfile", "w");
		if (file2 != NULL) { fprintf(file2, "logfile_%.4d_%s",file_count,timestp); fclose(file2); }
		
		// log filename
		sprintf(logfile1,"sailboat-log/logfile_%.4d_%s",file_count,timestp);
		//sprintf(logfile2,"sailboat-log/debug/debug_%.4d_%s",file_count,timestp);
		sprintf(logfile3,"sailboat-log/thesis/thesis_%.4d_%s",file_count,timestp);

		// write HEADERS in log files
		file2 = fopen(logfile1, "w");
		if (file2 != NULL) { fprintf(file2, "MCU_timestamp,Navigation_System,Manual_Control,Guidance_Heading,Manual_Ctrl_Rudder,Rudder_Desired_Angle,Rudder_Feedback,Manual_Ctrl_Sail,Sail_Desired_Pos,Sail_Feedback,Rate,Heading,Pitch,Roll,Latitude,Longitude,COG,SOG,Wind_Speed,Wind_Angle,Point_Start_Lat,Point_Start_Lon,Point_End_Lat,Point_End_Lon\n"); fclose(file2); }
		//file2 = fopen(logfile2, "w");
		//if (file2 != NULL) { fprintf(file2, "MCU_timestamp,sig1,sig2,sig3,fa_debug,theta_d1,theta_d,theta_d1_b,theta_b,a_x,b_x,X_b,X_T_b,sail_hc_periods,sail_hc_direction,sail_hc_val,sail_hc_MEAN_V,act_history,jibe_status\n"); fclose(file2); }
		file2 = fopen(logfile3, "w");
		if (file2 != NULL) { fprintf(file2, "MCU_timestamp,Navigation_System,Manual_Control,heading_state,sail_state,steptime,stepsize,vLOS,stepDIR,DIR_init,des_app_w,des_heading,sail_stepsize,sail_pos,des_slope,Wind_Angle,Wind_Speed,SOG,Heading,Roll,theta_mean_wind,ctri_sail,ctri_headsl,ctri_head,ctri_heel,u_sail,u_headsl,u_head,u_heel,headstep,desACTpos,Sail_Feedback\n"); fclose(file2); }
		
		logEntry=1;
	}

	// read rudder feedback
	file2 = fopen("/tmp/sailboat/Rudder_Feedback", "r");
	if (file2 != NULL) { fscanf(file2, "%d", &Rudder_Feedback); fclose(file2); }


	// generate csv LOG line
	sprintf(logline, "%u,%d,%d,%.1f,%d, %d,%d,%d,%d,%d, %.4f,%.4f,%.4f,%.4f,%f ,%f,%.1f,%.3f,%.2f,%.2f, %f,%f,%f,%f" \
		, (unsigned)time(NULL) \
		, Navigation_System \
		, Manual_Control \
		, Guidance_Heading \
		, Manual_Control_Rudder \
		, Rudder_Desired_Angle \
		, Rudder_Feedback \
		, Manual_Control_Sail \
		, Sail_Desired_Position \
		, Sail_Feedback \
		, Rate \
		, Heading \
		, Pitch \
		, Roll \
		, Latitude \
		, Longitude \
		, COG \
		, SOG \
		, Wind_Speed \
		, Wind_Angle \
		, Point_Start_Lat \
		, Point_Start_Lon \
		, Point_End_Lat \
		, Point_End_Lon \
	);
	// write to LOG file
	file2 = fopen(logfile1, "a");
	if (file2 != NULL) { fprintf(file2, "%s\n", logline); fclose(file2); }


	// generate csv DEBUG line
	/* sprintf(logline, "%u,%d,%d,%d,%d,%f,%f,%f,%f,%f,%f,%f_%fi,%f_%fi,%d" \
		, (unsigned)time(NULL) \
		, sig1 \
		, sig2 \
		, sig3 \
		, fa_debug \
		, theta_d1 \
		, theta_d \
		, theta_d1_b \
		, theta_b \
		, a_x \
		, b_x \
		, creal(X_T), cimag(X_T) \
		, creal(X_T_b), cimag(X_T_b) \
		, jibe_status \
	); */
	// write to DEBUG file
	//file2 = fopen(logfile2, "a");
	//if (file2 != NULL) { fprintf(file2, "%s\n", logline); fclose(file2); }
	
	
	// generate csv THESIS file
	sprintf(logline, "%u,%d,%d,%d,%d, %d,%d,%d,%d,%d, %d,%d,%d,%d,%f,%.2f, %.3f,%.3f,%.2f,%.3f,%.3f, %f,%f,%f,%f, %d,%d,%d,%d,%d,%d,%d" \
		, (unsigned)time(NULL) \
		, Navigation_System \
		, Manual_Control \
		, heading_state \
		, sail_state \
		
		, steptime \
		, stepsize \
		, vLOS \
		, stepDIR \
		, DIR_init \
		
		, des_app_w \
		, des_heading \
		, sail_stepsize \
		, sail_pos \
		, des_slope \
		, Wind_Angle \
		
		, Wind_Speed \
		, SOG \
		, Heading \
		, Roll \
		, theta_mean_wind \
		
		, ctri_sail \
		, ctri_headsl \
		, ctri_head \
		, ctri_heel \
		
		, u_sail \
		, u_headsl \
		, u_head \
		, u_heel \
		, headstep \
		, desACTpos \
		, Sail_Feedback \
		
	);
	// write to THESIS file
	file2 = fopen(logfile3, "a");
	if (file2 != NULL) { fprintf(file2, "%s\n", logline); fclose(file2); }
	

	fa_debug=0;
	logEntry++;
}
