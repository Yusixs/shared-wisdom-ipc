#include <iostream>
#include <cstdlib>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <fstream>
#include <cmath>
#include <sstream>
#include <unistd.h>
#include <semaphore.h>
#include <chrono>

using namespace std;

// Shared variables between threads, both are handled properly via mutex
int width = 0;
int neighborCount = 0;
pthread_mutex_t mutex;

// Structure For Exit door's shared memory
struct ExitInfo {
    int width;
    int x_position;
    int y_position;
};

// Structure for each robot's information in shared memory
struct RobotInfo {
    int robot_id;
    int x_coordinate;
    int y_coordinate;
    int distance_to_exit;
    int estimated_width;
};

// Thread parameters
struct ThreadRequiredInformation {
    int ownID;
    int neighbourID;
    int totalRobots;
    int verbose;
};

// Getting Maximum accuracy threshold
int getMinAccuracy(int unit) {
    int baseReduction = 5;
    int reductionStep = 2;
    if (unit <= 0) {
        return 100;
    } else if (unit == 1) {
        return 100 - baseReduction;
    } else {
        return getMinAccuracy(unit - 1) - baseReduction - (unit - 1) * reductionStep;
    }
}

// Getting minimum accuracy threshold
int getMaxAccuracy(int unit) {
    int baseReduction = 5;
    int reductionStep = 2;
    if (unit <= 1) {
        return 100;
    } else {
        return getMaxAccuracy(unit - 1) - baseReduction - (unit - 2) * reductionStep;
    }
}

// For every 5 units, decrease accuracy by 5%, up until 60% accuracy
int estimateExitWidth(int distance, int trueWidth) {

    int accuracyRange = 0;
    if (distance > 0)
        accuracyRange = (distance -1) / 5 + 1;

    int baseReduction = 5;
    int reductionStep = 2;

    int accuracyMin = max(60, getMinAccuracy(accuracyRange));
    int accuracyMax = max(68, getMaxAccuracy(accuracyRange));
    
    int randomPercent = 100 - (rand() % (accuracyMax - accuracyMin + 1) + accuracyMin);
    int reduction = (trueWidth * randomPercent) / 100;


    // Either add or subtract
    if (rand() % 2)
        return trueWidth + reduction;
    else
        return trueWidth - reduction;
}

// ========================== Thread for reading from other robots (Mutex for neighbor widths) =========================

void* robotThread(void* arg) {

    // Unbox Parameters (robot's own id, neighbor robot's id, total robot count)
    ThreadRequiredInformation* threadInformation = (ThreadRequiredInformation*)arg;
    int totalRobots = threadInformation->totalRobots;
    int ownID = threadInformation->ownID;
    int neighbourID = threadInformation->neighbourID;
    int verbose = threadInformation->verbose;

    // Creating key baased on filepath
    key_t key_robot = ftok("robotInfo", 0);

    // Error Handling
    if (key_robot == -1) {
        perror("Error in ftok for robotInfo file: ");
        return nullptr;
    }

    // Creating robot's shared memory id
    int robot_shmid = shmget(key_robot, sizeof(RobotInfo) * totalRobots, 0666);

    // Error Handling
    if (robot_shmid == -1) {
        perror("Error in shmget() for Robot SHMID: ");
        return nullptr;
    }

    // Attaching to pointer
    RobotInfo* robot_info = (RobotInfo*)shmat(robot_shmid, nullptr, 0);

    // Error Handling
    if (robot_info == (RobotInfo*)-1) {
        perror("Error in shmat() for RobotInfo");
        return nullptr;
    }

    // Verbose Logging
    if (verbose >= 2)
        printf("Robot %d is checking Robot %d's information | Exit Distance = %d | Estimated Width = %d\n", ownID, neighbourID, (robot_info + neighbourID)->distance_to_exit, (robot_info + neighbourID)->estimated_width);

    // Calculate Euclidean between each robot
    int distance = sqrt(pow((robot_info + neighbourID)->x_coordinate - (robot_info + ownID)->x_coordinate, 2) + 
                        pow((robot_info + neighbourID)->y_coordinate - (robot_info + ownID)->y_coordinate, 2));

    // If other robot is a neighbor, then accumulate its width with global width value (whilst ensuring synchronization)
    if (distance <= 5) {

        if (verbose >= 3)
            printf("Adding Width: %d\n", (robot_info + neighbourID)->estimated_width);
    
        // Mutex & width addition & neighbourcount increment
        pthread_mutex_lock(&mutex);
        width += (robot_info + neighbourID)->estimated_width;
        neighborCount++;
        pthread_mutex_unlock(&mutex);  
    }

    // Detaching and Error Handling
    if (shmdt(robot_info) == -1) {
        perror("Error in shmdt() for RobotInfo");
        return nullptr;
    }

    pthread_exit(nullptr);
}

int main(int argc, char* argv[]) {

    // =============== Initializations & other small stuff ========================

    // Record start and target time
    auto startTime = std::chrono::high_resolution_clock::now();
    auto targetTime = startTime + std::chrono::seconds(10);

    // Get Semaphores
    sem_t *sem = sem_open("waitTill50", 0);
    sem_t *globalWidthSemaphore = sem_open("globalwidth", 0);

    // Initialize mutex
    if (pthread_mutex_init(&mutex, NULL) != 0) { 
        printf("\n mutex init has failed\n"); 
        return 1; 
    } 

    // Randomizing seed again per process
    srand(time(0) * getpid());
    
    // Argument check
    if (argc != 6) {
        cerr << "Usage: " << argv[0] << " <robot_id> <x_coordinate> <y_coordinate> <total_robots> <verbose_level>" << endl;
        return 1;
    }

    // Unpacking arguments
    int robotId = atoi(argv[1]);
    int xCoordinate = atoi(argv[2]);
    int yCoordinate = atoi(argv[3]);
    int totalRobots = atoi(argv[4]);
    int verbose = atoi(argv[5]);


    // ======================= Reading exit info & robot info shared memory ===========================

    // Getting key value
    key_t key_exit = ftok("exitInfo", 0);

    // Error Handling
    if (key_exit == -1) {
        perror("Error in ftok for ExitInfo");
        return 1;
    }

    // Getting shared memory ID for exit info
    int exit_shmid = shmget(key_exit, sizeof(ExitInfo), 0666);
    if (exit_shmid == -1) {
        perror("Error in shmget() for ExitInfo");
        return 1;
    }

    // Attaching pointer to exitinfo shared memory
    ExitInfo* exit_info = (ExitInfo*)shmat(exit_shmid, nullptr, 0);

    // Error Handling
    if (exit_info == (ExitInfo*)-1) {
        perror("Error in shmat() for ExitInfo");
        return 1;
    }

    // Getting key value for robot info's shared memory    
    key_t key_robot = ftok("robotInfo", 0);
    
    // Error Handling
    if (key_robot == -1) {
        perror("Error in ftok for RobotInfo");
        return 1;
    }

    // Getting robot info's shared memory id
    int robot_shmid = shmget(key_robot, sizeof(RobotInfo) * totalRobots, 0666);
    
    // Error Handling
    if (robot_shmid == -1) {
        perror("Error in shmget() for RobotInfo");
        return 1;
    }

    // Attaching pointer to robot info's shared memory
    RobotInfo* robot_info = (RobotInfo*)shmat(robot_shmid, nullptr, 0);

    // Error Handling
    if (robot_info == (RobotInfo*)-1) {
        perror("Error in shmat() for RobotInfo");
        return 1;
    }

    // Writing own information into own allocated location in shared memory
    (robot_info + robotId)->robot_id = robotId;
    (robot_info + robotId)->x_coordinate = xCoordinate;
    (robot_info + robotId)->y_coordinate = yCoordinate;
    (robot_info + robotId)->distance_to_exit = sqrt(pow(xCoordinate - exit_info->x_position, 2) + pow(yCoordinate - exit_info->y_position, 2));
    (robot_info + robotId)->estimated_width = estimateExitWidth((robot_info + robotId)->distance_to_exit, exit_info->width);

    // Verbose logging
    if (verbose >= 1)
        printf("Robot %d is running with coords (%d, %d) | Exit Distance = %d units | True Width = %d | Estimated Width = %d\n", (robot_info + robotId)->robot_id, (robot_info + robotId)->x_coordinate, (robot_info + robotId)->y_coordinate, (robot_info + robotId)->distance_to_exit, exit_info->width, (robot_info + robotId)->estimated_width);

    // Detaching and Error Handling
    if (shmdt(exit_info) == -1) {
        perror("Error in shmdt() for ExitInfo");
        return 1;
    }

    // Detaching and Error Handling
    if (shmdt(robot_info) == -1) {
        perror("Error in shmdt() for RobotInfo");
        return 1;
    }

    // After writing coordinate, update counter
    sem_post(sem);

    // Check counter
    // Busy wait until all other robots are finished with their own uploading
    // (Otherwise, some robots will start reading -1 -1 -1 -1 information for robots which were slow in uploading!)    
    int value;
    do {
        sem_getvalue(sem, &value);
    } while (value < totalRobots);

    // ============================== Threads for reading every robot's values =========================

    pthread_t threads[totalRobots];
    ThreadRequiredInformation threadParams[totalRobots];

    // Setup parameters and start the threads
    for (int i = 0; i < totalRobots; ++i) {
        threadParams[i].ownID = robotId;
        threadParams[i].neighbourID = i;
        threadParams[i].totalRobots = totalRobots;
        threadParams[i].verbose = verbose;
        pthread_create(&threads[i], nullptr, robotThread, &threadParams[i]);
    }

    // Joining threads
    for (int i = 0; i < totalRobots; ++i) {
        pthread_join(threads[i], nullptr);
    }

    // Calculating its own width estimate
    int estimatedWidth = int(width / neighborCount);

    // Destroying mutex
    pthread_mutex_destroy(&mutex); 

    // Wait until 10 seconds have passed, then aggregate result with your fellow robots into a shared global variable
    while (std::chrono::high_resolution_clock::now() < targetTime);

    // Verbose logging
    if (verbose >= 1)
        printf("Robot %d is done. Estimated Width = %d Total Width = %d Total Neighbours (including itself) = %d\n", robotId, estimatedWidth, width, neighborCount);

    // ============================= Writing into global variable (semaphore) ======================

    // Wait if any other process is in this critical section
    sem_wait(globalWidthSemaphore);

    // Get global variable's key
    key_t key_width = ftok("estimatedGlobalWidth", 0);
    
    // Error Handling
    if (key_width == -1) {
        perror("Error in ftok: ");
        return 1;
    }
    
    // Get global width's shared memory id
    int width_shmid = shmget(key_width, sizeof(int), 0666);

    // Error Handling
    if (width_shmid == -1) {
        perror("Error in shmget() for Width: ");
        return 1;
    }

    // Attach global width to a pointer
    int* globalWidth = (int*)shmat(width_shmid, nullptr, 0);

    // Add your own estimated width into the global estimate
    *globalWidth += estimatedWidth;
         
    // Error Handling
    if (shmdt(globalWidth) == -1) {
        perror("Error in shmdt() for Width: ");
        return 1;
    }

    // Exit Critical section
    sem_post(globalWidthSemaphore);



    return 0;
}

