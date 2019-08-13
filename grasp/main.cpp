//
// 20141209: kcchang: changed window version to linux 

// myAllegroHand.cpp : Defines the entry point for the console application.
//
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>  //_getch
#include <string.h>
#include <pthread.h>
#include "canAPI.h"
#include "rDeviceAllegroHandCANDef.h"
#include <BHand/BHand.h>
#include "Sai2Model.h"
#include <signal.h>
#include <vector>
#include <deque>

using namespace std;

// ============= For REDIS =================

#include <hiredis/hiredis.h>
#include <iostream>

static const std::string ALLGERO_COMMAND = "allegro::command";

/** Global constants for REDIS host and port. */
static const std::string REDIS_HOST = "127.0.0.1";
static const int REDIS_PORT = 6379;


/** Global REDIS interface variables */
redisContext *GLOBAL_Redis_Context;
redisReply *GLOBAL_Redis_Reply;

bool initializeRedis()
{
  GLOBAL_Redis_Reply = NULL;
  GLOBAL_Redis_Context = redisConnect(REDIS_HOST.c_str(), REDIS_PORT);
  if (GLOBAL_Redis_Context->err) {
    std::cerr << "Error: " <<  GLOBAL_Redis_Context->errstr << std::endl;
    return false;
  } else {
    std::cout << "REDIS Connection Successful.\n" << std::endl;
  redisCommand(GLOBAL_Redis_Context, "SET %s o", ALLGERO_COMMAND.c_str());
    return true;
  }
}


char getCommandFromRedis()
{	
    GLOBAL_Redis_Reply = (redisReply *) redisCommand(GLOBAL_Redis_Context,
        "GET %s", ALLGERO_COMMAND.c_str());

    char buf = 0;
    sscanf(GLOBAL_Redis_Reply->str, "%c", &buf);
    freeReplyObject(GLOBAL_Redis_Reply);
	// printf("%c\n",buf);
    return buf;
}

// =========================================

typedef char    TCHAR;
#define _T(X)   X
#define _tcsicmp(x, y)   strcmp(x, y)

using namespace std;

/////////////////////////////////////////////////////////////////////////////////////////
// for CAN communication
const double delT = 0.003;
int CAN_Ch = 0;
bool ioThreadRun = false;
pthread_t        hThread;
pthread_t        cThread;  // control thread
int recvNum = 0;
int sendNum = 0;
double statTime = -1.0;
AllegroHand_DeviceMemory_t vars;

double curTime = 0.0;

/////////////////////////////////////////////////////////////////////////////////////////
// for custom pd controller
bool custom_PD = false;
double q_prev[MAX_DOF];
double dq[MAX_DOF];
double dq_filter_input[MAX_DOF];
double dq_prev_filter_input[MAX_DOF];
double dq_prev_prev_filter_input[MAX_DOF];
double dq_filtered[MAX_DOF];
double dq_prev_filtered[MAX_DOF];
double dq_prev_prev_filtered[MAX_DOF];

double kp_custom[] = {
  1.8, 1.8, 1.8, 1.8,
  1.8, 1.8, 1.8, 1.8,
  1.8, 1.8, 1.8, 1.8,
  1.8, 1.8, 1.8, 1.8
};
double kd_custom[] = {
  0.15, 0.15, 0.15, 0.15,
  0.15, 0.15, 0.15, 0.15,
  0.15, 0.15, 0.15, 0.15,
  0.15, 0.15, 0.15, 0.15
};

double q_pre_cube[] = {
  0.55, 1.65, 0.4, 0.3,   //finger 1
  0.15, 0.25, 0.65, 0.6,  // finger 2
  -0.4, 1.55, 0.4, 0.1,   // finger3
  1.65, -0.25, 0.0, 0.8   //finger 0  (thumb)
};

double q_cube[] = {
  0.55, 1.65, 0.4, 0.3,
  0.15, 0.25, 0.65, 0.6,
  -0.4, 1.55, 0.4, 0.1,
  1.65, -0.25, 0.0, 0.8
};

enum CustomGrasp {PreCubeGrasp, CubeGrasp};

CustomGrasp custom_grasp = PreCubeGrasp;

// filter
double gain = 2.419823131e+01;                              // 25 Hz
double filter_coeffs[] = {-0.5136414053, 1.3483400678};
// double gain = 4.020427297e+00;                                 // 75 Hz
// double filter_coeffs[] = {-0.1774700802, 0.1825509574};

unsigned long long counter = 0;

/////////////////////////////////////////////////////////////////////////////////////////
// for BHand library
BHand* pBHand = NULL;
double q[MAX_DOF];
double q_des[MAX_DOF];
double tau_des[MAX_DOF];
double cur_des[MAX_DOF];

// USER HAND CONFIGURATION
const bool	RIGHT_HAND = true;
const int	HAND_VERSION = 3;

const double tau_cov_const_v2 = 800.0; // 800.0 for SAH020xxxxx
const double tau_cov_const_v3 = 1200.0; // 1200.0 for SAH030xxxxx

//const double enc_dir[MAX_DOF] = { // SAH020xxxxx
//	1.0, -1.0, 1.0, 1.0,
//	1.0, -1.0, 1.0, 1.0,
//	1.0, -1.0, 1.0, 1.0,
//	1.0, 1.0, -1.0, -1.0
//};
//const double motor_dir[MAX_DOF] = { // SAH020xxxxx
//	1.0, 1.0, 1.0, 1.0,
//	1.0, -1.0, -1.0, 1.0,
//	-1.0, 1.0, 1.0, 1.0,
//	1.0, 1.0, 1.0, 1.0
//};
//const int enc_offset[MAX_DOF] = { // SAH020CR020
//	-611, -66016, 1161, 1377,
//	-342, -66033, -481, 303,
//	30, -65620, 446, 387,
//	-3942, -626, -65508, -66768
//};
//const int enc_offset[MAX_DOF] = { // SAH020BR013
//	-391,	-64387,	-129,	 532,
//	 178,	-66030,	-142,	 547,
//	-234,	-64916,	 7317,	 1923,
//	 1124,	-1319,	-65983, -65566
//};

const double enc_dir[MAX_DOF] = { // SAH030xxxxx
  1.0, 1.0, 1.0, 1.0,
  1.0, 1.0, 1.0, 1.0,
  1.0, 1.0, 1.0, 1.0,
  1.0, 1.0, 1.0, 1.0
};
const double motor_dir[MAX_DOF] = { // SAH030xxxxx
  1.0, 1.0, 1.0, 1.0,
  1.0, 1.0, 1.0, 1.0,
  1.0, 1.0, 1.0, 1.0,
  1.0, 1.0, 1.0, 1.0
};
//const int enc_offset[MAX_DOF] = { // SAH030AR023
//	-1700, -568, -3064, -36,
//	-2015, -1687, 188, -772,
//	-3763, 782, -3402, 368,
//	1059, -2547, -692, 2411
//};
//const int enc_offset[MAX_DOF] = { // SAH030AL026
//	699, 1654, 5, -464,
//	-47, 1640, 325, 687,
//	-361, 1161, -259, -510,
//	-1563, 569, 470, -812
//};
const int enc_offset[MAX_DOF] = { // SAH030C033R
  -1591, -277, 545, 168,
  -904, 53, -233, -1476,
  2, -987, -230, -106,
  -1203, 361, 327, 565
};

/////////////////////////////////////////////////////////////////////////////////////////
// functions declarations
char Getch();
void PrintInstruction();
void MainLoop();
bool OpenCAN();
void CloseCAN();
int GetCANChannelIndex(const TCHAR* cname);
bool CreateBHandAlgorithm();
void DestroyBHandAlgorithm();
void ComputeTorqueCustom();
void ComputeGravityTorque();

/////////////////////////////////////////////////////////////////////////////////////////
// Read keyboard input (one char) from stdin
char Getch()
{
  /*#include <unistd.h>   //_getch*/
  /*#include <termios.h>  //_getch*/
  char buf=0;
  struct termios old={0};
  fflush(stdout);
  if(tcgetattr(0, &old)<0)
    perror("tcsetattr()");
  old.c_lflag&=~ICANON;
  old.c_lflag&=~ECHO;
  old.c_cc[VMIN]=1;
  old.c_cc[VTIME]=0;
  if(tcsetattr(0, TCSANOW, &old)<0)
    perror("tcsetattr ICANON");
  if(read(0,&buf,1)<0)
    perror("read()");
  old.c_lflag|=ICANON;
  old.c_lflag|=ECHO;
  if(tcsetattr(0, TCSADRAIN, &old)<0)
    perror ("tcsetattr ~ICANON");
  printf("%c\n",buf);
  return buf;
}

/////////////////////////////////////////////////////////////////////////////////////////
// For code from Sai2
bool runloop = false;
void sighandler(int sig)
{ runloop = false; }

using namespace std;
using namespace Eigen;

const string robot_file = "hand.urdf";
const string robot_name = "Hand3Finger";


const std::string JOINT_ANGLES_KEY  = "sai2::graspFan::sensors::q";
const std::string JOINT_VELOCITIES_KEY = "sai2::graspFan::sensors::dq";
const std::string JOINT_TORQUES_COMMANDED_KEY = "sai2::graspFan::actuators::fgc";

#define NUM_OF_FINGERS_IN_MODEL 4
#define NUM_OF_FINGERS_USED     3

#define CONTACT_COEFFICIENT     0.5 
#define MIN_COLLISION_V         0.01
#define DISPLACEMENT_DIS        0.02  // how much you wanna move awat from the original point in normal detection step
#define FRICTION_COEFFICIENT     0.5

#define PRE_GRASP               0
#define FINGER_MOVE_CLOSE       1
#define DETECT_NORMAL           2
#define CHECK                   3
#define APPLY_FORCE             4
#define LIFT                    5

int state = PRE_GRASP;

double prob_distance = 0.006; // how much you want the to prob laterally in normal detection step

// the function used in the finger position control command
VectorXd compute_position_cmd_torques(Sai2Model::Sai2Model* robot, string link, Vector3d pos_in_link, Vector3d desired_position, double kp);
// the function used in the finger force control, used to achieve compliance
VectorXd compute_force_cmd_torques(Sai2Model::Sai2Model* robot, string link, Vector3d pos_in_link, Vector3d desired_position, double force_requeired);
// this function is used to detect surface normal by sampling several points in the vicinity
// It can only be called when the finger tip is making contact with the object surface
// returns the torque needed 
VectorXd detect_surface_normal(Sai2Model::Sai2Model* robot, string link, Vector3d pos_in_link, Vector3d original_pos, Vector3d CoM_of_object, int& state, deque<double>& velocity_record, vector<Vector3d>& contact_points, Vector3d& normal);
// this function is used to check whether we can only 2 finger to have a leagal grasp
bool check_2_finger_grasp(vector<Vector3d> contact_points,vector<Vector3d> normals, double friction_coefficient);
bool check_3_finger_grasp(vector<Vector3d> contact_points,vector<Vector3d> normals, double friction_coefficient);

static void sai2* (void* inst)
{
  // set up signal handler
  signal(SIGABRT, &sighandler);
  signal(SIGTERM, &sighandler);
  signal(SIGINT, &sighandler);

  double frequency = 1000;

  auto robot = new Sai2Model::Sai2Model(robot_file, false);

    // read from Redis
  robot->_q = redis_client.getEigenMatrixJSON(JOINT_ANGLES_KEY);
  robot->_dq = redis_client.getEigenMatrixJSON(JOINT_VELOCITIES_KEY);
  robot->updateModel();
  int dof = robot->dof();

  vector<Vector3d> current_finger_position;
  VectorXd command_torques = VectorXd::Zero(dof);
  vector<VectorXd> finger_command_torques;
  VectorXd palm_command_torques = VectorXd::Zero(dof);
  VectorXd coriolis = VectorXd::Zero(dof);
  MatrixXd N_prec = MatrixXd::Identity(dof,dof);
  finger_command_torques.push_back(VectorXd::Zero(dof));
  finger_command_torques.push_back(VectorXd::Zero(dof));
  finger_command_torques.push_back(VectorXd::Zero(dof));
  finger_command_torques.push_back(VectorXd::Zero(dof));
  vector<VectorXd> temp_finger_command_torques = finger_command_torques; // the raw command torques before blocking
  //control 4 fingers, finger 0,1,2,3

  // the vector used to record the velocity in the finger move close state
  vector<deque<double>> velocity_record;
  vector<deque<double>> detect_velocity_record; 


  // vector<Sai2Primitives::PositionTask *>  position_tasks;
  vector<int> detect_states;
  vector<vector<Vector3d>> contact_points;
  vector<Vector3d> normals;
  vector<string> link_names;
  vector<Affine3d> poses;
  Affine3d identity_pose = Affine3d::Identity();
  Affine3d temp_pose = Affine3d::Identity();
  temp_pose.translation() = Vector3d(0.0507,0.0,0.0);
  poses.push_back(temp_pose);
  temp_pose.translation() = Vector3d(0.0327, 0.0, 0.0);
  poses.push_back(temp_pose);
  poses.push_back(temp_pose);
  poses.push_back(temp_pose);

  link_names.push_back("finger0-link3");
  link_names.push_back("finger1-link3");
  link_names.push_back("finger2-link3");
  link_names.push_back("finger3-link3");

  Vector3d CoM_of_object = Vector3d(0.05,0.0,0.05); // in the world frame
  CoM_of_object -= Vector3d(0.0, 0.0, 0.25); // transform into the robor frame

  for(int i = 0; i < NUM_OF_FINGERS_USED; i++)
  {
    deque<double> temp_queue;
    temp_queue.push_back(0.0);
    temp_queue.push_back(0.0);
    velocity_record.push_back(temp_queue);
    detect_velocity_record.push_back(temp_queue);

    current_finger_position.push_back(Vector3d::Zero());

    detect_states.push_back(0);

    normals.push_back(Vector3d::Zero());
  }

  auto palm_posori_task = new Sai2Primitives::PosOriTask(robot, "palm", Vector3d(0.0,0.0,0.0));

  LoopTimer timer;
  timer.setLoopFrequency(frequency);
  timer.setCtrlCHandler(sighandler);
  //timer.initializeTimer(1000000);

  vector<int> finger_contact_flag; // finger0, 1, 2, 3
  for (int i = 0; i < NUM_OF_FINGERS_IN_MODEL; i++)
  {
    finger_contact_flag.push_back(0);
  }

  runloop = true ;
  int loop_counter = 0;


  // cout << robot->_joint_names_map["finger0-j0"] << "!!!!!!!!!!!!!!!!!!!!!!!" <<endl;
  while(runloop)
  {

    timer.waitForNextLoop();
    robot->_q = redis_client.getEigenMatrixJSON(JOINT_ANGLES_KEY);
    robot->_dq = redis_client.getEigenMatrixJSON(JOINT_VELOCITIES_KEY);
    //cout <<"q" << robot->_q << endl;
    robot->updateModel();
    robot->coriolisForce(coriolis);

    if (state == PRE_GRASP)
    {
      palm_posori_task->_desired_position = Vector3d(0.03,0.0,-0.08);
      N_prec.setIdentity();
      palm_posori_task->updateTaskModel(N_prec);
      N_prec = palm_posori_task->_N;
      palm_posori_task->computeTorques(palm_command_torques);
      //cout << "Here's the torque" << palm_command_torques << endl;
      temp_finger_command_torques[0] = compute_position_cmd_torques(robot, link_names[0], poses[0].translation(), Vector3d(-0.08, 0.0, -0.15), 10.0);
      temp_finger_command_torques[1] = compute_position_cmd_torques(robot, link_names[1], poses[1].translation(), Vector3d(0.15, -0.041, -0.2), 10.0);
        temp_finger_command_torques[2] = compute_position_cmd_torques(robot, link_names[2], poses[2].translation(), Vector3d(0.15, 0.0, -0.2), 10.0);
        temp_finger_command_torques[3] = compute_position_cmd_torques(robot, link_names[3], poses[3].translation(), Vector3d(0.15, 0.041, -0.09), 10.0);
            
        // block the unrelated torques
        finger_command_torques[0].block(6,0,4,1) = temp_finger_command_torques[0].block(6,0,4,1);
        finger_command_torques[1].block(10,0,4,1) = temp_finger_command_torques[1].block(10,0,4,1);
        finger_command_torques[2].block(14,0,4,1) = temp_finger_command_torques[2].block(14,0,4,1);
        finger_command_torques[3].block(18,0,4,1) = temp_finger_command_torques[3].block(18,0,4,1);


        if (palm_command_torques.norm() + finger_command_torques[0].norm() + finger_command_torques[1].norm() + finger_command_torques[2].norm() < 0.0001)
        {
          state = FINGER_MOVE_CLOSE;
        }
    }
    else if (state == FINGER_MOVE_CLOSE)
    { 
      // keep the position of the palm
      palm_posori_task->_desired_position = Vector3d(0.03,0.0,-0.08);
      N_prec.setIdentity();
      palm_posori_task->updateTaskModel(N_prec);
      N_prec = palm_posori_task->_N;
      palm_posori_task->computeTorques(palm_command_torques);

      // force controller for the fingers
      for(int i = 0; i < NUM_OF_FINGERS_USED; i++)
      {
        if (finger_contact_flag[i] == 0)
        {
          temp_finger_command_torques[i] = compute_force_cmd_torques(robot, link_names[i], poses[i].translation(), CoM_of_object, 0.001);
          finger_command_torques[i].block(6+4*i,0,4,1) = temp_finger_command_torques[i].block(6+4*i,0,4,1);
          Vector3d temp_finger_velocity = Vector3d::Zero();
          robot->linearVelocity(temp_finger_velocity, link_names[i], poses[i].translation());
          velocity_record[i].pop_front();
          velocity_record[i].push_back(temp_finger_velocity.norm());
          if (velocity_record[i][1]/velocity_record[i][0] < 0.5 && velocity_record[i][0] > MIN_COLLISION_V)
          {
            cout <<"finger "<< i <<" contact"<<endl;
            cout << "the previous velocity is: " << velocity_record[i][0] << endl;
            cout << "the current velocity is: " << velocity_record[i][1] << endl;
            finger_contact_flag[i] = 1;
            // set the desired position, maintain the current position
            robot->position(current_finger_position[i], link_names[i], poses[i].translation());
            // cout << current_finger_position[i] << endl;
          }
        }
        // maintain the current position after contact
        else if (finger_contact_flag[i] == 1)
        {
          temp_finger_command_torques[i] = compute_position_cmd_torques(robot, link_names[i], poses[i].translation(), current_finger_position[i], 10.0);
            finger_command_torques[i].block(6 + 4 * i ,0 ,4, 1) = temp_finger_command_torques[i].block(6 + 4 * i, 0 ,4 ,1 );
        } 
      }

      // keep the position of fingers that are not used
      for (int j = NUM_OF_FINGERS_USED; j < NUM_OF_FINGERS_IN_MODEL; j++)
      {
          temp_finger_command_torques[j] = compute_position_cmd_torques(robot, link_names[j], poses[j].translation(), Vector3d(0.15, 0.041, -0.09), 10.0);
        finger_command_torques[j].block(6+4*j,0,4,1) = temp_finger_command_torques[j].block(6+4*j,0,4,1);
      }

      int sum_of_contact = 0;
      for (int j = 0; j < NUM_OF_FINGERS_USED; j++)
      {
        sum_of_contact += finger_contact_flag[j];
      }
      if (sum_of_contact == NUM_OF_FINGERS_USED)
      {
        state = DETECT_NORMAL;
        for (int j = 0; j < NUM_OF_FINGERS_USED; j++)
        {
          contact_points.push_back({current_finger_position[j]});
        }
      }
    }

    else if (state == DETECT_NORMAL)
    {
      double sum_of_normal = 0.0;
      palm_posori_task->_desired_position = Vector3d(0.03,0.0,-0.08);
      N_prec.setIdentity();
      palm_posori_task->updateTaskModel(N_prec);
      N_prec = palm_posori_task->_N;
      palm_posori_task->computeTorques(palm_command_torques);
      for (int i = 0; i < NUM_OF_FINGERS_USED; i++)
      {
        temp_finger_command_torques[i] = detect_surface_normal(robot, link_names[i], poses[i].translation(), current_finger_position[i], CoM_of_object, detect_states[i], detect_velocity_record[i], contact_points[i], normals[i]);
        finger_command_torques[i].block(6 + 4 * i ,0 ,4, 1) = temp_finger_command_torques[i].block(6 + 4 * i, 0 ,4 ,1 );
        sum_of_normal += normals[i].norm();
/*        cout << normals[i] << endl;
        if (i == 0)
        {
          cout << finger_command_torques[i].block(6 + 4 * i ,0 ,4, 1) << endl;
        }*/
      }
/*      cout << endl << endl;
      if(loop_counter % 1000 == 0)
      {
        for(int j = 0; j < normals.size(); j++)
        {
          cout <<"normals" << j << ":\n" << normals[j] << endl;
        }
        cout << sum_of_normal << "!!!!!!" << endl;
      }*/
      for (int j = NUM_OF_FINGERS_USED; j < NUM_OF_FINGERS_IN_MODEL; j++)
      {
          temp_finger_command_torques[j] = compute_position_cmd_torques(robot, link_names[j], poses[j].translation(), Vector3d(0.15, 0.041, -0.09), 10.0);
        finger_command_torques[j].block(6+4*j,0,4,1) = temp_finger_command_torques[j].block(6 + 4 * j,0,4,1);
      }
      //cout << sum_of_normal << endl;
      if(sum_of_normal > double(NUM_OF_FINGERS_USED)-0.5)
      {
        cout << "all the normals detected" << endl;
        state = CHECK;
      }

    }

    else if (state == CHECK)
    {
      // check whether we can achieve 2 finger contact.
      if (check_2_finger_grasp(current_finger_position, normals, FRICTION_COEFFICIENT))
        {state = APPLY_FORCE;}


    }

    else if (state == APPLY_FORCE)
    {
      palm_posori_task->_desired_position = Vector3d(0.03,0.0,-0.08);
      palm_posori_task->_kp_force = 500.0;
      palm_posori_task->_kp_moment = 500.0;
      palm_posori_task->_kv_force = 10.0;
      palm_posori_task->_kv_moment = 10.0;
      palm_posori_task->_ki_force = 10.0;
      palm_posori_task->_ki_moment = 10.0;
      N_prec.setIdentity();
      palm_posori_task->updateTaskModel(N_prec);
      N_prec = palm_posori_task->_N;
      palm_posori_task->computeTorques(palm_command_torques);
      for(int j = 1; j < NUM_OF_FINGERS_USED; j++)
      {
        temp_finger_command_torques[j] = compute_position_cmd_torques(robot, link_names[j], poses[j].translation(), current_finger_position[j], 100.0);
      }
      robot->position(current_finger_position[0], link_names[0], poses[0].translation());
      temp_finger_command_torques[0] = compute_force_cmd_torques(robot, link_names[0], poses[0].translation(), current_finger_position[0] + normals[0], 0.3);
      for(int j = 0; j < NUM_OF_FINGERS_USED; j++)
      {
          finger_command_torques[j].block(6 + 4 * j ,0 ,4, 1) = temp_finger_command_torques[j].block(6 + 4 * j, 0 ,4 ,1 );
      }

      for (int j = NUM_OF_FINGERS_USED; j < NUM_OF_FINGERS_IN_MODEL; j++)
      {
          temp_finger_command_torques[j] = compute_position_cmd_torques(robot, link_names[j], poses[j].translation(), Vector3d(0.15, 0.041, -0.09), 10.0);
        finger_command_torques[j].block(6+4*j,0,4,1) = temp_finger_command_torques[j].block(6+4*j,0,4,1);
      }
    }

    loop_counter++;

  command_torques = finger_command_torques[0] + finger_command_torques[1] \
  + finger_command_torques[2] + finger_command_torques[3]\
  + palm_command_torques + coriolis;
  //cout << command_torques << endl;
  redis_client.setEigenMatrixJSON(JOINT_TORQUES_COMMANDED_KEY, command_torques);
    // reset to 0
  for(int i =0; i < NUM_OF_FINGERS_IN_MODEL; i++)
  {
    temp_finger_command_torques[i].setZero();
    finger_command_torques[i].setZero();
  }
  command_torques.setZero();
  }

  command_torques.setZero();
    redis_client.setEigenMatrixDerived(JOINT_TORQUES_COMMANDED_KEY, command_torques);

    double end_time = timer.elapsedTime();
    std::cout << "\n";
    std::cout << "Loop run time  : " << end_time << " seconds\n";
    std::cout << "Loop updates   : " << timer.elapsedCycles() << "\n";
    std::cout << "Loop frequency : " << timer.elapsedCycles()/end_time << "Hz\n";

}

/////////////////////////////////////////////////////////////////////////////////////////
// CAN communication thread
static void* ioThreadProc(void* inst)
{
  char id_des;
  char id_cmd;
  char id_src;
  int len;
  unsigned char data[8];
  unsigned char data_return = 0;
  int i;

  while (ioThreadRun)
    {
      /* wait for the event */
      while (0 == get_message(CAN_Ch, &id_cmd, &id_src, &id_des, &len, data, FALSE))
	{
	  switch (id_cmd)
	    {
	    case ID_CMD_QUERY_ID:
	      {
		printf(">CAN(%d): AllegroHand revision info: 0x%02x%02x\n", CAN_Ch, data[3], data[2]);
		printf("                      firmware info: 0x%02x%02x\n", data[5], data[4]);
		printf("                      hardware type: 0x%02x\n", data[7]);
	      }
	      break;

	    case ID_CMD_AHRS_POSE:
	      {
		printf(">CAN(%d): AHRS Roll : 0x%02x%02x\n", CAN_Ch, data[0], data[1]);
		printf("               Pitch: 0x%02x%02x\n", data[2], data[3]);
		printf("               Yaw  : 0x%02x%02x\n", data[4], data[5]);
	      }
	      break;

	    case ID_CMD_AHRS_ACC:
	      {
		printf(">CAN(%d): AHRS Acc(x): 0x%02x%02x\n", CAN_Ch, data[0], data[1]);
		printf("               Acc(y): 0x%02x%02x\n", data[2], data[3]);
		printf("               Acc(z): 0x%02x%02x\n", data[4], data[5]);
	      }
	      break;

	    case ID_CMD_AHRS_GYRO:
	      {
		printf(">CAN(%d): AHRS Angular Vel(x): 0x%02x%02x\n", CAN_Ch, data[0], data[1]);
		printf("               Angular Vel(y): 0x%02x%02x\n", data[2], data[3]);
		printf("               Angular Vel(z): 0x%02x%02x\n", data[4], data[5]);
	      }
	      break;

	    case ID_CMD_AHRS_MAG:
	      {
		printf(">CAN(%d): AHRS Magnetic Field(x): 0x%02x%02x\n", CAN_Ch, data[0], data[1]);
		printf("               Magnetic Field(y): 0x%02x%02x\n", data[2], data[3]);
		printf("               Magnetic Field(z): 0x%02x%02x\n", data[4], data[5]);
	      }
	      break;

	    case ID_CMD_QUERY_CONTROL_DATA:
	      {
		if (id_src >= ID_DEVICE_SUB_01 && id_src <= ID_DEVICE_SUB_04)
		  {
		    vars.enc_actual[(id_src-ID_DEVICE_SUB_01)*4 + 0] = (int)(data[0] | (data[1] << 8));
		    vars.enc_actual[(id_src-ID_DEVICE_SUB_01)*4 + 1] = (int)(data[2] | (data[3] << 8));
		    vars.enc_actual[(id_src-ID_DEVICE_SUB_01)*4 + 2] = (int)(data[4] | (data[5] << 8));
		    vars.enc_actual[(id_src-ID_DEVICE_SUB_01)*4 + 3] = (int)(data[6] | (data[7] << 8));
		    data_return |= (0x01 << (id_src-ID_DEVICE_SUB_01));
		    recvNum++;
		  }
		if (data_return == (0x01 | 0x02 | 0x04 | 0x08))
		  {
		    // convert encoder count to joint angle
		    for (i=0; i<MAX_DOF; i++)
		      {
			q[i] = (double)(vars.enc_actual[i]*enc_dir[i]-32768-enc_offset[i])*(333.3/65536.0)*(3.141592/180.0);
		      }

        // compute joint torque
          ComputeGravityTorque(); 

/*        cout << "here is the tau_des: \n" ;
        for (int j=0; j < MAX_DOF; j++)
        {
          cout << tau_des[j] << endl;
        }
        cout << endl;*/

		    // convert desired torque to desired current and PWM count

		    for (i=0; i<MAX_DOF; i++)
		      {
			cur_des[i] = tau_des[i] * motor_dir[i];
			if (cur_des[i] > 1.0) cur_des[i] = 1.0;
			else if (cur_des[i] < -1.0) cur_des[i] = -1.0;
		      }

		    // send torques
		    for (int i=0; i<4;i++)
		      {
			// the index order for motors is different from that of encoders
			switch (HAND_VERSION)
			  {
			  case 1:
			  case 2:
			    vars.pwm_demand[i*4+3] = (short)(cur_des[i*4+0]*tau_cov_const_v2);
			    vars.pwm_demand[i*4+2] = (short)(cur_des[i*4+1]*tau_cov_const_v2);
			    vars.pwm_demand[i*4+1] = (short)(cur_des[i*4+2]*tau_cov_const_v2);
			    vars.pwm_demand[i*4+0] = (short)(cur_des[i*4+3]*tau_cov_const_v2);
			    break;

			  case 3:
			  default:
			    vars.pwm_demand[i*4+3] = (short)(cur_des[i*4+0]*tau_cov_const_v3);
			    vars.pwm_demand[i*4+2] = (short)(cur_des[i*4+1]*tau_cov_const_v3);
			    vars.pwm_demand[i*4+1] = (short)(cur_des[i*4+2]*tau_cov_const_v3);
			    vars.pwm_demand[i*4+0] = (short)(cur_des[i*4+3]*tau_cov_const_v3);
			    break;
			  }
			write_current(CAN_Ch, i, &vars.pwm_demand[4*i]);
			usleep(5);
		      }
		    sendNum++;
		    curTime += delT;

		    data_return = 0;
		  }
	      }
	      break;
	    }
	}
    }

  return NULL;
}

/////////////////////////////////////////////////////////////////////////////////////////
// Application main-loop. It handles the commands from rPanelManipulator and keyboard events
void MainLoop()
{

  bool bRun = true;

  if(!initializeRedis()) {cout<<"Redis initialization failed";exit(1);}
  int prev_c = 0;
  pBHand->SetMotionType(eMotionType_GRAVITY_COMP);
  while (bRun)
    {
      int c = Getch();    
      // int c = getCommandFromRedis();
      if (prev_c != c)
      { 
      custom_PD = false;
		  printf("%c\n",c);
		  prev_c = c;
	      switch (c)
	        {
	        case 'q':
		  if (pBHand) pBHand->SetMotionType(eMotionType_NONE);
		  bRun = false;
		  break;
      case 's':
      int ioThread_error = pthread_create(&cThread, NULL, sai2, 0);

/*
	        case 'h':
		  if (pBHand) pBHand->SetMotionType(eMotionType_HOME);
		  break;

	        case 'r':
		  if (pBHand) pBHand->SetMotionType(eMotionType_READY);
		  break;

	        case 'g':
		  if (pBHand) pBHand->SetMotionType(eMotionType_GRASP_3);
		  break;

	        case 'k':
		  if (pBHand) pBHand->SetMotionType(eMotionType_GRASP_4);
		  break;

	        case 'p':
		  if (pBHand) pBHand->SetMotionType(eMotionType_PINCH_IT);
		  break;

	        case 'm':
		  if (pBHand) pBHand->SetMotionType(eMotionType_PINCH_MT);
		  break;

	        case 'a':
		  if (pBHand) pBHand->SetMotionType(eMotionType_GRAVITY_COMP);
		  break;

	        case 'e':
		  if (pBHand) pBHand->SetMotionType(eMotionType_ENVELOP);
		  break;

	        case 'o':
		  if (pBHand) pBHand->SetMotionType(eMotionType_NONE);
		  break;

          case '1':
      if(pBHand)
      {
        custom_PD = true;
        custom_grasp = PreCubeGrasp;
      }
      break;
          case '2':
      if(pBHand)
      {
        custom_PD = true;
        custom_grasp = CubeGrasp;
      }
      break;*/

	        }
      // usleep(100000);
	    }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////
// Compute control torque for each joint using BHand library
void ComputeGravityTorque()
{
  if (!pBHand) return;
  pBHand->SetJointPosition(q); // tell BHand library the current joint positions
  pBHand->SetJointDesiredPosition(q_des);  // this line isn't needed if we are using some grasping mode defined by the library
  pBHand->UpdateControl(0);
  pBHand->GetJointTorque(tau_des);
}

/////////////////////////////////////////////////////////////////////////////////////////
// Compute control torque for each joint using custom pd controller
void ComputeTorqueCustom()
{
  if (!pBHand) return;


  // get velocity and filter
  for (int i=0; i<MAX_DOF; i++)    
  {
    dq[i] = (q[i] - q_prev[i]) / delT;
    dq_filter_input[i] = dq[i]/gain;
    dq_filtered[i] = dq_prev_prev_filter_input[i] + 2*dq_prev_filter_input[i] + dq_filter_input[i] + filter_coeffs[0]*dq_prev_prev_filtered[i] + filter_coeffs[1]*dq_prev_filtered[i];
  }

  if(custom_grasp == PreCubeGrasp)
  {
    for(int i=0; i<MAX_DOF; i++)
    {
      q_des[i] = q_pre_cube[i];
    }  
    for(int i=0; i<MAX_DOF; i++)
    {
      tau_des[i] = -kp_custom[i]*(q[i]-q_des[i]) - kd_custom[i]*dq_filtered[i];
    }
  }
  else if(custom_grasp == CubeGrasp)
  {
    for(int i=0; i<MAX_DOF; i++)
    {
      q_des[i] = q_cube[i];
    }  
    for(int i=0; i<MAX_DOF; i++)
    {
      tau_des[i] = -kp_custom[i]*(q[i]-q_des[i]) - kd_custom[i]*dq_filtered[i];
    }
    tau_des[0] = -1.5;
    tau_des[5] = 1.9;
    tau_des[8] = 1.5;
    // tau_des[15] = 0.9;
  }





  // if(counter % 500 == 0)
  // {
  //   std::cout << "q : " << std::endl;
  //   for(int i=0; i<4; i++)
  //   {
  //     std::cout << q[4*i] << '\t' << q[4*i+1] << '\t' << q[4*i+2] << '\t' << q[4*i+3] << '\n';
  //   }
  //   std::cout << std::endl;
  //   std::cout << "q_des : " << std::endl;
  //   for(int i=0; i<4; i++)
  //   {
  //     std::cout << q_des[4*i] << '\t' << q_des[4*i+1] << '\t' << q_des[4*i+2] << '\t' << q_des[4*i+3] << '\n';
  //   }
  //   std::cout << std::endl;
  //   std::cout << "dq : " << std::endl;
  //   for(int i=0; i<4; i++)
  //   {
  //     std::cout << dq[4*i] << '\t' << dq[4*i+1] << '\t' << dq[4*i+2] << '\t' << dq[4*i+3] << '\n';
  //   }
  //   std::cout << std::endl;    
  //   std::cout << "dq filtered : " << std::endl;
  //   for(int i=0; i<4; i++)
  //   {
  //     std::cout << dq_filtered[4*i] << '\t' << dq_filtered[4*i+1] << '\t' << dq_filtered[4*i+2] << '\t' << dq_filtered[4*i+3] << '\n';
  //   }
  //   std::cout << std::endl;
  //   std::cout << "tau : " << std::endl;
  //   for(int i=0; i<4; i++)
  //   {
  //     std::cout << tau_des[4*i] << '\t' << tau_des[4*i+1] << '\t' << tau_des[4*i+2] << '\t' << tau_des[4*i+3] << '\n';
  //   }
  //   std::cout << std::endl << std::endl;
  // }

  // save last iteration info for filter
  for (int i=0; i<MAX_DOF; i++)
  {
    q_prev[i] = q[i];
    dq_prev_prev_filter_input[i] = dq_prev_filter_input[i];
    dq_prev_filter_input[i] = dq_filter_input[i];
    dq_prev_prev_filtered[i] = dq_prev_filtered[i];
    dq_prev_filtered[i] = dq_filtered[i];
  }

  counter++;
}

/////////////////////////////////////////////////////////////////////////////////////////
// Open a CAN data channel
bool OpenCAN()
{
#if defined(PEAKCAN)
  CAN_Ch = GetCANChannelIndex(_T("USBBUS1"));
#elif defined(IXXATCAN)
  CAN_Ch = 1;
#elif defined(SOFTINGCAN)
  CAN_Ch = 1;
#else
  CAN_Ch = 1;
#endif
  CAN_Ch = GetCANChannelIndex(_T("USBBUS1"));
  printf(">CAN(%d): open\n", CAN_Ch);

  int ret = command_can_open(CAN_Ch);
  if(ret < 0)
    {
      printf("ERROR command_canopen !!! \n");
      return false;
    }

  ioThreadRun = true;

  /* initialize condition variable */
  int ioThread_error = pthread_create(&hThread, NULL, ioThreadProc, 0);
  if (ioThread_error)
    {
      printf("error, the io thread starting procedure failed.\n");
    }
    
  printf(">CAN: starts listening CAN frames\n");

  printf(">CAN: query system id\n");
  ret = command_can_query_id(CAN_Ch);
  if(ret < 0)
    {
      printf("ERROR command_can_query_id !!! \n");
      command_can_close(CAN_Ch);
      return false;
    }

  printf(">CAN: AHRS set\n");
  ret = command_can_AHRS_set(CAN_Ch, AHRS_RATE_100Hz, AHRS_MASK_POSE | AHRS_MASK_ACC);
  if(ret < 0)
    {
      printf("ERROR command_can_AHRS_set !!! \n");
      command_can_close(CAN_Ch);
      return false;
    }

  printf(">CAN: system init\n");
  ret = command_can_sys_init(CAN_Ch, 3/*msec*/);
  if(ret < 0)
    {
      printf("ERROR command_can_sys_init !!! \n");
      command_can_close(CAN_Ch);
      return false;
    }

  printf(">CAN: start periodic communication\n");
  ret = command_can_start(CAN_Ch);

  if(ret < 0)
    {
      printf("ERROR command_can_start !!! \n");
      command_can_stop(CAN_Ch);
      command_can_close(CAN_Ch);
      return false;
    }

  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////
// Close CAN data channel
void CloseCAN()
{
  printf(">CAN: stop periodic communication\n");
  int ret = command_can_stop(CAN_Ch);
  if(ret < 0)
    {
      printf("ERROR command_can_stop !!! \n");
    }

  if (ioThreadRun)
    {
      printf(">CAN: stoped listening CAN frames\n");
      ioThreadRun = false;
      int status;
      pthread_join(hThread, (void **)&status);
      hThread = 0;
    }

  printf(">CAN(%d): close\n", CAN_Ch);
  ret = command_can_close(CAN_Ch);
  if(ret < 0) printf("ERROR command_can_close !!! \n");
}

/////////////////////////////////////////////////////////////////////////////////////////
// Load and create grasping algorithm
bool CreateBHandAlgorithm()
{
  if (RIGHT_HAND)
    pBHand = bhCreateRightHand();
  else
    pBHand = bhCreateLeftHand();

  if (!pBHand) return false;
  pBHand->SetTimeInterval(delT);
  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////
// Destroy grasping algorithm
void DestroyBHandAlgorithm()
{
  if (pBHand)
    {
#ifndef _DEBUG
      delete pBHand;
#endif
      pBHand = NULL;
    }
}

////////////////////////////////////////////////////////////////////////////////////////
// Print program information and keyboard instructions
void PrintInstruction()
{
  printf("--------------------------------------------------\n");
  printf("myAllegroHand: ");
  if (RIGHT_HAND) printf("Right Hand, v%i.x\n\n", HAND_VERSION); else printf("Left Hand, v%i.x\n\n", HAND_VERSION);

  printf("Keyboard Commands:\n");
  printf("H: Home Position (PD control)\n");
  printf("R: Ready Position (used before grasping)\n");
  printf("G: Three-Finger Grasp\n");
  printf("K: Four-Finger Grasp\n");
  printf("P: Two-finger pinch (index-thumb)\n");
  printf("M: Two-finger pinch (middle-thumb)\n");
  printf("E: Envelop Grasp (all fingers)\n");
  printf("A: Gravity Compensation\n\n");

  printf("O: Servos OFF (any grasp cmd turns them back on)\n");
  printf("Q: Quit this program\n");

  printf("--------------------------------------------------\n\n");
}
/////////////////////////////////////////////////////////////////////////////////////////
// Get channel index for Peak CAN interface
int GetCANChannelIndex(const TCHAR* cname)
{
  if (!cname) return 0;

  if (!_tcsicmp(cname, _T("0")) || !_tcsicmp(cname, _T("PCAN_NONEBUS")) || !_tcsicmp(cname, _T("NONEBUS")))
    return 0;
  else if (!_tcsicmp(cname, _T("1")) || !_tcsicmp(cname, _T("PCAN_ISABUS1")) || !_tcsicmp(cname, _T("ISABUS1")))
    return 1;
  else if (!_tcsicmp(cname, _T("2")) || !_tcsicmp(cname, _T("PCAN_ISABUS2")) || !_tcsicmp(cname, _T("ISABUS2")))
    return 2;
  else if (!_tcsicmp(cname, _T("3")) || !_tcsicmp(cname, _T("PCAN_ISABUS3")) || !_tcsicmp(cname, _T("ISABUS3")))
    return 3;
  else if (!_tcsicmp(cname, _T("4")) || !_tcsicmp(cname, _T("PCAN_ISABUS4")) || !_tcsicmp(cname, _T("ISABUS4")))
    return 4;
  else if (!_tcsicmp(cname, _T("5")) || !_tcsicmp(cname, _T("PCAN_ISABUS5")) || !_tcsicmp(cname, _T("ISABUS5")))
    return 5;
  else if (!_tcsicmp(cname, _T("7")) || !_tcsicmp(cname, _T("PCAN_ISABUS6")) || !_tcsicmp(cname, _T("ISABUS6")))
    return 6;
  else if (!_tcsicmp(cname, _T("8")) || !_tcsicmp(cname, _T("PCAN_ISABUS7")) || !_tcsicmp(cname, _T("ISABUS7")))
    return 7;
  else if (!_tcsicmp(cname, _T("8")) || !_tcsicmp(cname, _T("PCAN_ISABUS8")) || !_tcsicmp(cname, _T("ISABUS8")))
    return 8;
  else if (!_tcsicmp(cname, _T("9")) || !_tcsicmp(cname, _T("PCAN_DNGBUS1")) || !_tcsicmp(cname, _T("DNGBUS1")))
    return 9;
  else if (!_tcsicmp(cname, _T("10")) || !_tcsicmp(cname, _T("PCAN_PCIBUS1")) || !_tcsicmp(cname, _T("PCIBUS1")))
    return 10;
  else if (!_tcsicmp(cname, _T("11")) || !_tcsicmp(cname, _T("PCAN_PCIBUS2")) || !_tcsicmp(cname, _T("PCIBUS2")))
    return 11;
  else if (!_tcsicmp(cname, _T("12")) || !_tcsicmp(cname, _T("PCAN_PCIBUS3")) || !_tcsicmp(cname, _T("PCIBUS3")))
    return 12;
  else if (!_tcsicmp(cname, _T("13")) || !_tcsicmp(cname, _T("PCAN_PCIBUS4")) || !_tcsicmp(cname, _T("PCIBUS4")))
    return 13;
  else if (!_tcsicmp(cname, _T("14")) || !_tcsicmp(cname, _T("PCAN_PCIBUS5")) || !_tcsicmp(cname, _T("PCIBUS5")))
    return 14;
  else if (!_tcsicmp(cname, _T("15")) || !_tcsicmp(cname, _T("PCAN_PCIBUS6")) || !_tcsicmp(cname, _T("PCIBUS6")))
    return 15;
  else if (!_tcsicmp(cname, _T("16")) || !_tcsicmp(cname, _T("PCAN_PCIBUS7")) || !_tcsicmp(cname, _T("PCIBUS7")))
    return 16;
  else if (!_tcsicmp(cname, _T("17")) || !_tcsicmp(cname, _T("PCAN_PCIBUS8")) || !_tcsicmp(cname, _T("PCIBUS8")))
    return 17;
  else if (!_tcsicmp(cname, _T("18")) || !_tcsicmp(cname, _T("PCAN_USBBUS1")) || !_tcsicmp(cname, _T("USBBUS1")))
    return 18;
  else if (!_tcsicmp(cname, _T("19")) || !_tcsicmp(cname, _T("PCAN_USBBUS2")) || !_tcsicmp(cname, _T("USBBUS2")))
    return 19;
  else if (!_tcsicmp(cname, _T("20")) || !_tcsicmp(cname, _T("PCAN_USBBUS3")) || !_tcsicmp(cname, _T("USBBUS3")))
    return 20;
  else if (!_tcsicmp(cname, _T("21")) || !_tcsicmp(cname, _T("PCAN_USBBUS4")) || !_tcsicmp(cname, _T("USBBUS4")))
    return 21;
  else if (!_tcsicmp(cname, _T("22")) || !_tcsicmp(cname, _T("PCAN_USBBUS5")) || !_tcsicmp(cname, _T("USBBUS5")))
    return 22;
  else if (!_tcsicmp(cname, _T("23")) || !_tcsicmp(cname, _T("PCAN_USBBUS6")) || !_tcsicmp(cname, _T("USBBUS6")))
    return 23;
  else if (!_tcsicmp(cname, _T("24")) || !_tcsicmp(cname, _T("PCAN_USBBUS7")) || !_tcsicmp(cname, _T("USBBUS7")))
    return 24;
  else if (!_tcsicmp(cname, _T("25")) || !_tcsicmp(cname, _T("PCAN_USBBUS8")) || !_tcsicmp(cname, _T("USBBUS8")))
    return 25;
  else if (!_tcsicmp(cname, _T("26")) || !_tcsicmp(cname, _T("PCAN_PCCBUS1")) || !_tcsicmp(cname, _T("PCCBUS1")))
    return 26;
  else if (!_tcsicmp(cname, _T("27")) || !_tcsicmp(cname, _T("PCAN_PCCBUS2")) || !_tcsicmp(cname, _T("PCCBUS2")))
    return 271;
  else
    return 0;
}


/////////////////////////////////////////////////////////////////////////////////////////
// Program main
int main(int argc, TCHAR* argv[])
{
  PrintInstruction();

  memset(&vars, 0, sizeof(vars));
  memset(q, 0, sizeof(q));
  memset(q_des, 0, sizeof(q_des));
  memset(tau_des, 0, sizeof(tau_des));
  memset(cur_des, 0, sizeof(cur_des));
  curTime = 0.0;

  Sai2Model::Sai2Model* robot_model = new Sai2Model::Sai2Model("hand.urdf");

  if (CreateBHandAlgorithm() && OpenCAN())
    MainLoop();

  CloseCAN();
  DestroyBHandAlgorithm();

  return 0;
}


//////////////////////////////////////////////////////////////////////////////////////////
// functions from Sai2 program
VectorXd compute_force_cmd_torques(Sai2Model::Sai2Model* robot, string link, Vector3d pos_in_link, Vector3d desired_position, double force_requeired)
{
  int dof = robot->dof();
  // double force_requeired = 0.001;
  double damping = -force_requeired / 10;
  Vector3d current_position; // in robot frame
  Vector3d current_velocity;
  Vector3d desired_force;
  MatrixXd Jv = MatrixXd::Zero(3,dof);
  robot->Jv(Jv, link, pos_in_link);
  robot->position(current_position, link, pos_in_link);
  desired_force = desired_position - current_position;
  desired_force = desired_force / desired_force.norm(); // normalization
  desired_force = desired_force * force_requeired;
  VectorXd torque = VectorXd::Zero(dof);
  torque = Jv.transpose()*desired_force + damping * robot->_dq;
  return torque;
}

VectorXd detect_surface_normal(Sai2Model::Sai2Model* robot, string link, Vector3d pos_in_link, Vector3d original_pos, Vector3d CoM_of_object, int& state, deque<double>& velocity_record, vector<Vector3d>& contact_points, Vector3d& normal)
{
  int dof = robot->dof();
  VectorXd torque = VectorXd::Zero(dof);
  Vector3d current_position = Vector3d::Zero();
  robot->position(current_position, link, pos_in_link);
  if(state == 0) // just start from the initial centroid position
  {

    Vector3d desired_position = DISPLACEMENT_DIS*(original_pos - CoM_of_object) / (original_pos - CoM_of_object).norm() + \
    original_pos + Vector3d(0.0, 0.0, prob_distance);
    torque = compute_position_cmd_torques(robot, link, pos_in_link, desired_position, 10.0);
    if((desired_position - current_position).norm() < 0.001)
    {
      state = 1;
    }
  }
  else if (state == 1) // has reached the first intermediate point
  {
    torque = compute_force_cmd_torques(robot, link, pos_in_link, CoM_of_object, 0.001);
    Vector3d temp_finger_velocity = Vector3d::Zero();
    robot->linearVelocity(temp_finger_velocity, link, pos_in_link);
    velocity_record.pop_front();
    velocity_record.push_back(temp_finger_velocity.norm());
    if (velocity_record[1]/velocity_record[0] < CONTACT_COEFFICIENT && velocity_record[0] > MIN_COLLISION_V)
    {
      state = 2;
      cout << link <<" contact"<<endl;
      cout<< "the previous velocity is: " << velocity_record[0] << endl;
      cout << "the current velocity is: " << velocity_record[1] << endl;
      contact_points.push_back(current_position);
    }
  }

  else if(state == 2) 
  {

    Vector3d desired_position = DISPLACEMENT_DIS*(original_pos - CoM_of_object) / (original_pos - CoM_of_object).norm() + \
    original_pos + Vector3d(0.0, 0.0, -prob_distance);
    torque = compute_position_cmd_torques(robot, link, pos_in_link, desired_position, 10.0);
    if((desired_position - current_position).norm() < 0.001)
    {
      state = 3;
    }
  }

  else if (state == 3) // has reached the second intermediate point
  {
    torque = compute_force_cmd_torques(robot, link, pos_in_link, CoM_of_object, 0.001);
    Vector3d temp_finger_velocity = Vector3d::Zero();
    robot->linearVelocity(temp_finger_velocity, link, pos_in_link);
    velocity_record.pop_front();
    velocity_record.push_back(temp_finger_velocity.norm());
    if (velocity_record[1]/velocity_record[0] < CONTACT_COEFFICIENT && velocity_record[0] > MIN_COLLISION_V)
    {
      state = 4;
      cout << link <<" contact"<<endl;
      cout<< "the previous velocity is: " << velocity_record[0] << endl;
      cout << "the current velocity is: " << velocity_record[1] << endl;
      contact_points.push_back(current_position); 
      // cout << contact_points[100] << "test" << endl;   
    }
  }

  else if(state == 4) 
  {
    Vector3d disp = Vector3d(0.0, 0.0, 0.0);
    Vector3d origin_disp = (original_pos - CoM_of_object) / (original_pos - CoM_of_object).norm();
    disp[0] = origin_disp[1]/sqrt(pow(origin_disp[0], 2) + pow(origin_disp[1], 2));
    disp[1] = - origin_disp[0]/sqrt(pow(origin_disp[0], 2) + pow(origin_disp[1], 2));

    Vector3d desired_position = DISPLACEMENT_DIS*(original_pos - CoM_of_object) / (original_pos - CoM_of_object).norm() + \
    original_pos + prob_distance * disp;
    torque = compute_position_cmd_torques(robot, link, pos_in_link, desired_position, 10.0);
    if((desired_position - current_position).norm() < 0.001)
    {
      state = 5;
    }
  }

  else if (state == 5) // has reached the second intermediate point
  {
    torque = compute_force_cmd_torques(robot, link, pos_in_link, CoM_of_object, 0.001);
    Vector3d temp_finger_velocity = Vector3d::Zero();
    robot->linearVelocity(temp_finger_velocity, link, pos_in_link);
    velocity_record.pop_front();
    velocity_record.push_back(temp_finger_velocity.norm());
    if (velocity_record[1]/velocity_record[0] < CONTACT_COEFFICIENT && velocity_record[0] > MIN_COLLISION_V)
    {
      state = 6;
      cout << link <<" contact"<<endl;
      cout<< "the previous velocity is: " << velocity_record[0] << endl;
      cout << "the current velocity is: " << velocity_record[1] << endl;

      contact_points.push_back(current_position); 
      // cout << contact_points[100] << "test" << endl;   
    }
  }

  else if(state == 6) 
  {
    Vector3d disp = Vector3d(0.0, 0.0, 0.0);
    Vector3d origin_disp = (original_pos - CoM_of_object) / (original_pos - CoM_of_object).norm();
    disp[0] = origin_disp[1]/sqrt(pow(origin_disp[0], 2) + pow(origin_disp[1], 2));
    disp[1] = - origin_disp[0]/sqrt(pow(origin_disp[0], 2) + pow(origin_disp[1], 2));
    disp = - disp;

    Vector3d desired_position = DISPLACEMENT_DIS*(original_pos - CoM_of_object) / (original_pos - CoM_of_object).norm() + \
    original_pos + prob_distance * disp;
    torque = compute_position_cmd_torques(robot, link, pos_in_link, desired_position, 10.0);
    if((desired_position - current_position).norm() < 0.001)
    {
      state = 7;
    }
  }

  else if (state == 7) // has reached the fourth intermediate point
  {
    torque = compute_force_cmd_torques(robot, link, pos_in_link, CoM_of_object, 0.001);
    Vector3d temp_finger_velocity = Vector3d::Zero();
    robot->linearVelocity(temp_finger_velocity, link, pos_in_link);
    velocity_record.pop_front();
    velocity_record.push_back(temp_finger_velocity.norm());
    if (velocity_record[1]/velocity_record[0] < CONTACT_COEFFICIENT && velocity_record[0] > MIN_COLLISION_V)
    {
      state = 8;
      cout << link <<" contact "<<endl;
      cout<< "the previous velocity is: " << velocity_record[0] << endl;
      cout << "the current velocity is: " << velocity_record[1] << endl;

      contact_points.push_back(current_position); 
      // cout << contact_points[100] << "test" << endl;   
    }
  }

  else if (state == 8) // go back to the original contact position
  {
    torque = compute_position_cmd_torques(robot, link, pos_in_link, original_pos, 10.0);
    if((original_pos - current_position).norm() < 0.01)
    {
      state = 9;
    }
  }

  else if (state == 9)  // compute the normal
  {
    cout << "I am computing the normal for "<< link << endl; 
    // cout << "contact_points" << endl;
    Matrix3d coefficient_matrix = Matrix3d::Zero();
    Vector3d mean_position = Vector3d(0.0, 0.0, 0.0);
    auto centralized_position = contact_points;
    for (int j = 0; j < contact_points.size(); j++ )
    {
      mean_position += contact_points[j];
      // cout << contact_points[j] <<endl<< endl;
    }
    mean_position /= contact_points.size();
    for (int j = 0; j < contact_points.size(); j++)
    {
      centralized_position[j] -= mean_position;
    }
    for (int j = 0; j < contact_points.size(); j++)
    {
      coefficient_matrix += centralized_position[j] * centralized_position[j].transpose();
    }
    EigenSolver<Matrix3d> solver(coefficient_matrix);
    Matrix3d eigen_matrix = solver.eigenvectors().real();
    Vector3d eigen_values = solver.eigenvalues().real();
    int min_index = 999;
    double min_value = 999;
    for (int j = 0; j < 3; j++)
    {
      if (eigen_values[j] < min_value)
      {
        min_value = eigen_values[j];
        min_index = j;
      }
    }
    normal = eigen_matrix.real().col(min_index);
    // the following code chose which direction should the normal choose
    // it's the direction position to the CoM
    Vector3d temp = CoM_of_object - original_pos;
    // cout << normal << endl << endl;
    if ( temp.dot(normal) < 0)
    {
      normal = -normal; // opposite position
    }
    cout << "Here is the normal for finger " << link << endl << normal << endl << endl;
    state = 10;
    // cout << "the normal is "<< link << endl << normal << endl << endl;
    // cout << "the eigen vectors are" << endl << eigen_matrix << endl;
    // cout << "the eigen values are " << endl << eigen_values << endl;
  }
  else if (state == 10) // maintain the original contact position
  {
    torque = compute_position_cmd_torques(robot, link, pos_in_link, original_pos, 10.0);
  }
  return torque;
}

bool check_2_finger_grasp(vector<Vector3d> contact_points,vector<Vector3d> normals, double friction_coefficient)
{
  double alpha;
  alpha = atan(friction_coefficient);
  Vector3d connect_vector = Vector3d::Zero();
  contact_points.push_back(contact_points[0]);
  normals.push_back(normals[0]);
  int flag = 0;
  for(int i = 0; i < NUM_OF_FINGERS_USED; i++)
  {
    flag = 0;
    connect_vector = contact_points[i+1] - contact_points[i];
    if(normals[i].dot(connect_vector)/(normals[i].norm() * connect_vector.norm()) > cos(alpha));
    {
      flag++;
    }
    if(-normals[i+1].dot(connect_vector)/(normals[i+1].norm() * connect_vector.norm()) < cos(alpha))
    {
      flag++;
    }
    if (flag == 2)
    {
      return true;
    }
  }
  return false;
}

bool check_3_finger_grasp(vector<Vector3d> contact_points,vector<Vector3d> normals, double friction_coefficient)
{
  return true;
}
