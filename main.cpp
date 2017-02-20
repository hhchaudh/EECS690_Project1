// Haaris Chaudhry
// February 19, 2017
// main.cpp for Project 1

#include <vector>
#include <chrono>
#include <mutex>
#include <thread>
#include <iostream>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include <string>
#include <functional>
#include <atomic>
#include "Barrier.h"

std::mutex mtx;
std::atomic<int> numFinishedThreads;
std::atomic<bool> changedNumExpected;
std::mutex numExpectedLock;

// The value used to keep track of how many threads (trains) are in the move function
std::atomic<int> numExpected;

// The output lock required so that output messages aren't mixed together
std::mutex coutMutex;

// The barrier that will keep all threads synchronized through each timestep
Barrier aBarrier;

// A flag to to set a barrier on the first run of move()
std::atomic<bool> firstRun;

// Train struct, used as a container for each individual train
struct Train
{
    // The paths vector for each train is used to contain the tracks that each train will utilize
    // It keeps the tracks in order, so we know which path to take (or not take) during each timestep
    std::vector<std::unordered_set<int>> paths;

    // The name of the train will be a character in the range [A-Z]
    // This will be set in main()
    char name;

    // A station vector is used to identify each individual station that the train has to reach, in order
    // This is useful for outputting the current whereabouts of the train.  We can't use the paths vector
    // because each path is an unordered set, and therefore we can't ascertain which station is "first" in each path
    std::vector<int> stations;
};

// Track struct, used as a container for each individual track
struct Track
{
    // An individual path that can be compared against the paths in each train to determine whether this track should be utilized
    std::unordered_set<int> path;

    // If this track is utilized by a train, then we need to lock it, preventing other trains from using it
    std::mutex trackLock;
};

// The move thread is utilized by each thread (which represents a train) to move each train. It takes a Train and a vector of Tracks
// as parameters, the vector of Tracks contains all of the possibe Tracks that can be utilized according to the schedules of each train.
// The vector of Tracks is calculated in main(). Each Track in the vector has a unique path.
void move( Train& aTrain, std::vector<Track>& aTracks )
{
    if( firstRun )
    {
        aBarrier.barrier( numExpected );
        firstRun = false;
    }

    // Although each train has its own timestep, the timesteps should be in-sync because of the use of a barrier
    int timeStep = 0;

    // We'll compare each path of each train to the tracks contained in aTracks
    for( unsigned int i = 0; i < aTrain.paths.size(); i++ )
    {
        if( numExpectedLock.try_lock() )
        {
            if( !changedNumExpected )
            {
                numExpected -= numFinishedThreads;
                numFinishedThreads = 0;
                changedNumExpected = true;
            }
            numExpectedLock.unlock();
        }

        for( unsigned int j = 0; j < aTracks.size(); j++ )
        {
            // If we find a track that matches the current train path (and this should always happen) then we'll first
            // try to lock the track.  If we can lock the track, that means that the current thread can "move its train".
            // Otherwise, the current thread has to "hold its train".  We break out of the loop after matching because
            // each Track is unique, and therefore there are no more possible matches to be made.
            if( aTrain.paths[i] == aTracks[j].path )
            {
                if( aTracks[j].trackLock.try_lock() )
                {
                    coutMutex.lock();
                    std::cout << "At time step " << timeStep << ": ";
                    std::cout << "Train " << aTrain.name << " is moving from station " << aTrain.stations[i] << " to station " << aTrain.stations[i + 1] << std::endl;
                    coutMutex.unlock();
                    aTracks[j].trackLock.unlock();
                    break;
                }
                else
                {
                    coutMutex.lock();
                    std::cout << "At time step " << timeStep << ": ";
                    std::cout << "Train " << aTrain.name << " is waiting at station " << aTrain.stations[i] << std::endl;
                    coutMutex.unlock();
                    i--;  // If we had to hold, then we need to make sure that we don't move on to the next path
                    break;
                }
            }
        }

        if( i == ( aTrain.paths.size() - 1 ) )
        {
            numFinishedThreads++;
            coutMutex.lock();
            coutMutex.unlock();
        }

        //std::this_thread::sleep_for (std::chrono::microseconds(1));
        // Increment the timestep
        timeStep++;

        // Hit the barrier, wait on all threads to complete before moving on
        aBarrier.barrier( numExpected );
        changedNumExpected = false;
        aBarrier.barrier( numExpected );
    }
}

int main( int argc, char* argv[] )
{
    // Open the file to be read
    std::ifstream fileReader;

    if( argc > 1 )
    {
        fileReader.open( argv[1] );
        {
            if( !fileReader.is_open() )
            {
                std::cout << "Failed to open file, ending...\n";
                exit( 0 );
            }
        }
    }
    else
    {
        fileReader.open( "data.txt" );

        if( !fileReader.is_open() )
        {
            std::cout << "Failed to open file, ending...\n";
            exit( 0 );
        }
    }

    // The total number of trains
    int numTrains;

    // The total number of stations
    int numStations;

    // Each line that is read in the file will be examined one by one,
    // we'll store these lines in the line variable
    std::string line;

    // We'll read integers from the line string using iss
    std::istringstream iss;

    // Each train will be stored in this vector
    std::vector<Train> trains;

    // Each unique path that we discover will be stored in this vector
    std::vector<std::unordered_set<int>> paths;

    // Read the first line
    getline( fileReader, line );

    // Use iss to put line in a buffer
    iss.str( line );

    // The first line should contain the numTrains and numStations values
    iss >> numTrains;
    iss >> numStations;

    // Now we can read each train, which is done by reading line by line
    while( getline( fileReader, line ) )
    {
        // Create a new buffer and store line inside of it
        std::istringstream newSS( line );

        // The following values are used on each line to set the paths and stations variables for each
        // train
        int nextStation = -1;
        int secondStation = -1;
        int previousStation = -1;
        int trainSize;
        Train train;

        // The first value in the line is the trainSize
        newSS >> trainSize;

        // The rest of the values are the stations
        while( newSS >> nextStation )
        {
            train.stations.push_back( nextStation );

            // This loop accomplishes three things:
            //  1. Sets the paths variable for each train by pushing each new path on to the train paths vector
            //  2. Sets the stations variable for each train by pushing each station on to the train stations vector
            //  3. Stores each unique path that is found into the paths vector, which will be used later to
            //      store each individual Track.  We have to do this later because each Track has a lock, and
            //      locks are not movable so push_back cannot be used, therefore, we need to know how many
            //      Tracks to create before creating the tracks vector
            if( secondStation == -1 && newSS >> secondStation )
            {
                train.stations.push_back( secondStation );
                std::unordered_set<int> path( { nextStation, secondStation } );
                train.paths.push_back( path );
                previousStation = secondStation;
                if( paths.empty() )
                {
                    paths.push_back( path );
                }
                else
                {
                    bool pathFound = false;
                    for( auto& p : paths )
                    {
                        if( p == path )
                        {
                            pathFound = true;
                        }
                    }

                    // If we don't find the path, then it needs to be added to the paths vector
                    if( !pathFound )
                    {
                        paths.push_back( path );
                    }
                }
            }
            else
            {
                std::unordered_set<int> path( { previousStation, nextStation } );
                train.paths.push_back( path );
                previousStation = nextStation;

                bool pathFound = false;
                for( auto& p : paths )
                {
                    if( p == path )
                    {
                        pathFound = true;
                    }
                }

                if( !pathFound )
                {
                    paths.push_back( path );
                }
            }
        }
        // Put the train in the trains vector, we'll need this when we launch our threads
        trains.push_back( train );
    }

    // Create a name for each train that we've created, starting with the letter 'A'
    for( unsigned int i = 0; i < trains.size(); i++ )
    {
        trains[i].name = 65 + i;
    }

    // Create the tracks vector, which is possible now that we know how many we need
    std::vector<Track> tracks( paths.size() );

    // Store each individual unique path into a Track, therefore making each Track unique
    for( unsigned int i = 0; i < tracks.size(); i++ )
    {
        tracks[i].path = paths[i];
    }

    // Create the threads
    std::thread** t = new std::thread*[numTrains];

    // Set the number of expected threads
    numExpected = numTrains;

    firstRun = true;
    numFinishedThreads = 0;
    changedNumExpected = false;

    // Launch the threads, hitting a barrier first to make sure all threads are created before executing
    for( int i = 0; i < numTrains; i++ )
    {
        t[i] = new std::thread( move, std::ref( trains[i] ), std::ref( tracks ) );
    }

    // Join the threads back into the main thread before closing
    for( int i = 0; i < numTrains; i++ )
    {
        t[i]->join();
    }

    for( int i = 0; i < numTrains; i++ )
    {
        delete t[i];
    }

    delete t;

    return 0;
}
