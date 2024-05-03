#include <iostream>
#include <cstdlib>
#include <ctime>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <fstream>
#include <semaphore.h>
#include <fcntl.h>

using namespace std;

// Operating Systems Project. 

// Group Members:
//  Sarim Aeyzaz
//  Qasim Saeed
//  Hafiz Hammad Ahmed
//  Ehtsham Walidad


// Structures for storing information in Shared Memory 
struct ExitInfo {
    int width;          // Ranges between 16 and 26
    int x_position;     
    int y_position;
};

struct RobotInfo {
    int robot_id;           // Assigned during Process creation
    int x_coordinate;
    int y_coordinate;
    int distance_to_exit;   // Euclidean Distance metric b/w Robot's (x,y) coordinate and Exit's (x,y) coordinate
    int estimated_width;    // Estimated width w.r.t distance and accuracy metrics
};

int main() {

    // ============================= Changeable Variables ============================



    // Change this to however many robots you like :)
    const int num_robots = 50;

    // LOGGING OUTPUT 
    //  0 = Silent, 
    //  1 = Robot creation & width aggregate, 
    //  2 = Process Communication between all other Processes
    int verbose = 0;

    // Custom welcome message
    printf("Welcome to the robot room :). Please wait 10 seconds while our %d robots figure out how wide the exit is.\n", num_robots);

    // ============== Adding Exit Door's Information to Shared Memory =================

    // Assign random seed value
    srand(time(0));

    // Assign Exit's Width
    const int exitWidth = (rand() % 11) + 16;

    // Create file
    ofstream tempFile("exitInfo");
    tempFile.close();

    // Use ftok with created file name
    key_t key = ftok("exitInfo", 0);
    
    // Error Handling
    if (key == -1) {
        perror("Error in ftok for exitInfo file: ");
        return 1;
    }
    
    // Get Shared Memory ID (It will create it every single time)
    int exit_shmid = shmget(key, sizeof(ExitInfo), IPC_CREAT | 0666);

    // Error Handling
    if (exit_shmid == -1) {
        perror("Error in shmget() for ExitInfo: ");
        return 1;
    }
 
    // Attach Shared Memory to pointer
    ExitInfo* exit_info = (ExitInfo*)shmat(exit_shmid, nullptr, 0);

    // Error Hanlding
    if (exit_info == (ExitInfo*)-1) {
        perror("Error in shmat() for ExitInfo: ");
        return 1;
    }

    // Adding Exit door's information to shared memory
    exit_info->width = exitWidth;
    exit_info->x_position = 99;
    exit_info->y_position = 50;

    // Outputting Initial values
    if (verbose > 1)
        printf("Initial Exit door values | Width = %d | X Position = %d | Y Position = %d", exitWidth, exit_info->x_position, exit_info->y_position);
         
    // Detaching shared memory & Error Handling
    if (shmdt(exit_info) == -1) {
        perror("Error in shmdt() for ExitInfo: ");
        return 1;
    }

    // ====================== Created Robot's shared memory and flushing it ========================

    // Creating Shared Memory and Flushing it initially to -1S
    ofstream memoryFile("robotInfo");
    memoryFile.close();

    // Creating key baased on filepath
    key_t key_robot = ftok("robotInfo", 0);

    // Error Handling
    if (key_robot == -1) {
        perror("Error in ftok for robotInfo file: ");
        return 1;
    }

    // Creating robot's shared memory id
    int robot_shmid = shmget(key_robot, sizeof(RobotInfo) * num_robots, IPC_CREAT | 0666);

    // Error Handling
    if (robot_shmid == -1) {
        perror("Error in shmget() for Robot SHMID: ");
        return 1;
    }

    // Attaching to pointer
    RobotInfo* robot_info = (RobotInfo*)shmat(robot_shmid, nullptr, 0);

    // Error Handling
    if (robot_info == (RobotInfo*)-1) {
        perror("Error in shmat() for RobotInfo");
        return 1;
    }

    // Flushing Memory with -1's
    for (int i = 0; i < num_robots; i++) {
        (robot_info + i)->robot_id = -1;
        (robot_info + i)->distance_to_exit = -1;
        (robot_info + i)->estimated_width = -1;
        (robot_info + i)->x_coordinate = -1;
        (robot_info + i)->y_coordinate = -1;
    }

    // Detach and Error Handling
    if (shmdt(robot_info) == -1) {
        perror("Error in shmdt() for RobotInfo");
    }

    // ================== Global Width variable's shared memory ==============

    // Creating file
    ofstream temp("estimatedGlobalWidth");
    temp.close();

    // Use ftok with that file name created earlier
    key_t key_width = ftok("estimatedGlobalWidth", 0);
    
    // Error Handling
    if (key_width == -1) {
        perror("Error in ftok for estimated global width: ");
        return 1;
    }
    
    // Creating shared memory ID for global width
    int width_shmid = shmget(key_width, sizeof(int), IPC_CREAT | 0666);

    // Error Handling
    if (width_shmid == -1) {
        perror("Error in shmget() for Width: ");
        return 1;
    }
 
    // Attaching shared memory to pointer
    int* globalWidth = (int*)shmat(width_shmid, nullptr, 0);

    // Initializing global width to 0
    *globalWidth = 0;
    
    // Detaching and Error Handling
    if (shmdt(globalWidth) == -1) {
        perror("Error in shmdt() for Width: ");
        return 1;
    }

    // ================= Semaphores Initialization and Main function ======================

    // Semaphore decleration and initialization
    sem_t *sem = sem_open("waitTill50", O_CREAT, 0666, 1);
    
    sem_t *globalWidthSemaphore = sem_open("globalwidth", O_CREAT, 0666, 1);

    // Semaphore initialization, the 1 in the middle means that it's processor shared (not local) and the last 1 is its starting value
    sem_init(sem, 1, 0);
    sem_init(globalWidthSemaphore, 1, 1);

    // Create Process for 50 robots
    for (int i = 0; i < num_robots; ++i) {

        // New seed because the seed doesn't change fast enough :(
    	srand(time(0)+i);

        // Create Child Process which executes robot.cpp
        pid_t pid = fork();
	
        // Error Handling
        if (pid < 0) {
            cerr << "Error in fork(): ";
            return 1;
        }

        // Child Process
        if (pid == 0) {
            
            // Assign robot ID and random coordinate between 0 and 99
            int robot_id = i;
            int x_coord = rand() % 100;
            int y_coord = rand() % 100;
		
            if (verbose >= 2)
    		    cout<<x_coord<<" "<<y_coord<<endl;

            // Convert to arguments to string
            char robot_id_str[10], x_coord_str[10], y_coord_str[10], totalRobots_str[10], verbose_str[10];
            snprintf(robot_id_str, sizeof(robot_id_str), "%d", robot_id);
            snprintf(x_coord_str, sizeof(x_coord_str), "%d", x_coord);
            snprintf(y_coord_str, sizeof(y_coord_str), "%d", y_coord);
            snprintf(totalRobots_str, sizeof(totalRobots_str), "%d", num_robots);
            snprintf(verbose_str, sizeof(verbose_str), "%d", verbose);

            // Process launches robot.cpp with arguments (Its own id, x coord, y coord and total robots count)
            execl("./robot", "robot", robot_id_str, x_coord_str, y_coord_str, totalRobots_str, verbose_str, nullptr);

            // Error Handling            
            cerr << "Error in exec(): " << strerror(errno) << endl;

            // Child Process exits after robot.cpp code finishes
            exit(1);
        }
    }

    // ======================= Parent Process ===========================

    // Wait for all child processes to complete
    for (int i = 0; i < num_robots; ++i) {
        wait(NULL);
    }

    // Reattach global width shared memory to pointer
    globalWidth = (int*)shmat(width_shmid, nullptr, 0);

    // Calculate final estimate
    int finalEstimatedWidth = *globalWidth / num_robots;
    int difference = abs(exitWidth - finalEstimatedWidth);

    // Outputting answer
    printf("\nTrue Width = %d units | Estimated Width = %d units | Difference = %d units\n\n", exitWidth, finalEstimatedWidth, difference);
         
    // Detaching and Error Handling
    if (shmdt(globalWidth) == -1) {
        perror("Error in shmdt() for Width: ");
        return 1;
    }
    
    // ==================== Deleting Shared Memories & Semaphores ========================
    
    if (verbose > 1)
        printf("Deleting shared memories created...");
    
    // Removing Shared ID's
    if (shmctl(exit_shmid, IPC_RMID, nullptr) == -1) {
        perror("shmctl");
        return 1;
    }
    if (shmctl(robot_shmid, IPC_RMID, nullptr) == -1) {
        perror("shmctl");
        return 1;
    }
    if (shmctl(width_shmid, IPC_RMID, nullptr) == -1) {
        perror("shmctl");
        return 1;
    }

    // Detroying Semaphores
    sem_destroy(sem);
    sem_destroy(globalWidthSemaphore);

    return 0;
}

