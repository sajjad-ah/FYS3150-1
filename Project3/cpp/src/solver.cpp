#include <iostream>
#include <fstream>
#include <ctime>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <memory>
#include <algorithm>
#include <armadillo>
#include <jsoncpp/json/json.h>
#include "solver.h"
#include "solarSys.h"
#include "planet.h"
#include "Vec3/vec3.h"

#define G 39.478417604357434475337963

using std::placeholders::_1;

Solver::Solver(const std::string& parameterpath){
    readParameters(parameterpath);
}

int Solver::solve(){
    // Wrapper for initializing, solving and saving the solution of the system.
    initSystem();

    std::function<void(std::shared_ptr<Planet>)> stepOne;
    std::function<void(std::shared_ptr<Planet>)> stepTwo;

    switch (method){
    case Method::EULER:
      std::cout << "=== Simulating system with Euler's method ===" << std::endl;
      stepOne = std::bind(&Solver::nop, this, _1);
      stepTwo = std::bind(&Solver::EulerStep, this, _1);
      solveSystem(stepOne, stepTwo);
      break;
    case Method::VERLET:
      std::cout << "=== Simulating system with Velocity Verlet method ===" << std::endl;
      stepOne = std::bind(&Solver::VerletStep1, this, _1);
      stepTwo = std::bind(&Solver::VerletStep2, this, _1);
      solveSystem(stepOne, stepTwo);
      break;
    case Method::EULERCROMER:
      std::cout << "=== Simulating system with Euler-Cromer's method ===" << std::endl;
      stepOne = std::bind(&Solver::nop, this, _1);
      stepTwo = std::bind(&Solver::ECStep, this, _1);
      solveSystem(stepOne, stepTwo);
      break;
    default:
      std::cout << "=== NO METHOD CHOSEN ===" << std::endl;
      return -1;
    }
    saveToFile();
    return 0;
}

void Solver::solveSystem(std::function<void(std::shared_ptr<Planet>)>& stepOne,
                         std::function<void(std::shared_ptr<Planet>)>& stepTwo){
    // Solves the time development of the system. To generalize the code for
    // both algorithms, the forwarding functions are passed as args. The first
    // step is only used for the Verlet method because the acceleration is
    // updated mid-step.
    startTiming();
    sys.updateCOM();
    updateForces();
    updateEnergy(0);
    angMomArray(0) = sys.angularMomentum().length();
    double progress = 0.1;
    // Loop over time
    for(unsigned int i = 1; i <= n; i++){
        for(unsigned int j = 0; j < sys.planets.size(); j++){
            if(freezeSun && j == 0) continue;
            stepOne(sys.planets[j]);
        }

        // Loop over every planet to find acceleration of each planet
        updateForces();
        // Forward planet positions in time with method of choice
        // The first 'planet' is the sun, and receives special treatment
        for(unsigned int j = 0; j < sys.planets.size(); j++){
            if(freezeSun && j == 0){
                if(doSaveStep(i))
                    sys.planets[j]->writePosToMat(saveStep);
                continue;
            }
            stepTwo(sys.planets[j]);
            if(doSaveStep(i))
                sys.planets[j]->writePosToMat(saveStep);
        }
        // Write out the progress for each 10% increment
        if(i/static_cast<double>(n) >= progress){
            std::cout << 100*progress << "% completed" << std::endl;
            progress += 0.1;
        }
        if(doSaveStep(i)){
            updateEnergy(saveStep);
            angMomArray(saveStep) = sys.angularMomentum().length();
        }
        if(findPerihelionPrecession)
            measurePerihelion(i);
        sys.updateCOM();
    }
    endTiming();
}

void Solver::updateForces(){
    // Update the acceleration for each planet
    for(auto& planet: sys.planets){
        // Save previous acceleration for Verlet solver, reset temp variables
        planet->acc_prev = planet->acc;
        planet->resetAcc();
        planet->resetF();

        // Loop over all the other planets and add acceleration contribution
        for(auto & other: sys.planets){
            if(planet == other)continue;
            else{
                planet->calculateAcc(*other);
            }
        }
    }
}

void Solver::EulerStep(std::shared_ptr<Planet> planet){
    planet->vel_tmp = planet->vel;   // Save velocity temporarily for Euler
    planet->vel    += planet->acc*dt;
    planet->pos    += planet->vel_tmp*dt;
}

void Solver::VerletStep1(std::shared_ptr<Planet> planet){
    planet->pos += planet->vel*dt + 0.5*dt*dt*planet->acc;
}

void Solver::VerletStep2(std::shared_ptr<Planet> planet){
    planet->vel += 0.5*dt*(planet->acc + planet->acc_prev);
}

void Solver::ECStep(std::shared_ptr<Planet> planet){
    planet->vel += planet->acc*dt;
    planet->pos += planet->vel*dt;
}

void Solver::updateEnergy(unsigned int step){
    energyArray(0, step) = step*dt;
    energyArray(1, step) = sys.kineticEnergy();
    energyArray(2, step) = sys.potentialEnergy();
}

void Solver::measurePerihelion(unsigned int step) {
    bool isAtPerihelion = false;
    double theta = sys.planets[1]->getPerihelionPrecessionAngle(*sys.planets[0], isAtPerihelion);
    if(isAtPerihelion){
        double time = step*dt;
        std::cout << "Perihelion angle " << std::setprecision(15) << theta  << std::endl;
        perihelions.push_back(std::pair<double, double>(time, theta));
    }
}

void Solver::readParameters(const std::string& filename){
    // Load the JSON file and read the relevant parameters
    std::ifstream parameters(filename);
    parameters >> root;
    std::string smethod     = root["method"].asString();
    unsigned int N_per_year = root["steps per year"].asInt();
    unsigned int num_years  = root["number of years"].asInt();
    saveFlag                = root["do save results"].asBool();
    use_all_planets         = root["use all planets"].asBool();
    freezeSun               = root["freeze sun"].asBool();
    twoBodyApproximation    = root["use two body approximation"].asBool();
    relativisticCorrection  = root["use relativistic correction"].asBool();
    findPerihelionPrecession= root["find perihelion precession"].asBool();
    gravitationalExponent   = root["gravitational exponent"].asDouble();
    planets_to_use          = root["use planets"];
    savePeriod              = root["save period"].asInt();
    saveAnyResults          = root["do save any results"].asBool();

    // Act on the read parameters
    n = num_years*N_per_year;
    dt = 1.0/N_per_year;

    std::transform(smethod.begin(), smethod.end(), smethod.begin(), ::tolower);

    std::cout << "Simulating " << num_years << " year(s) with "
              << N_per_year << " steps per year, for a total of "
              << n << " steps and a time step of " << dt << std::endl;

    if(relativisticCorrection)
        std::cout << "Using relativistic mechanics" << std::endl;
    else
        std::cout << "Using Newtonian mechanics" << std::endl;

    if(saveAnyResults){
        saveableSteps = n/savePeriod;
        int loss      = n % savePeriod;
        if(saveFlag){
            saveableSteps = n;
            std::cout << "Saving all data" << std::endl;
        } else {
            std::cout << "Saving data each " << savePeriod << " step." << std::endl;
            std::cout << "Saving " << saveableSteps << " steps with a loss of " << loss << " steps." << std::endl;
        }
    } else {
        saveableSteps = 1;
        std::cout << "Not saving any data." << std::endl;
    }

    if(findPerihelionPrecession)
        std::cout << "Finding perihelion precession" << std::endl;


    if(smethod == "euler")
        method = Method::EULER;
    else if(smethod == "verlet")
        method = Method::VERLET;
    else
        throw std::runtime_error("Unknown method");

}

void Solver::initSystem(){
    // Add relevant objects to solar system instance, which holds all objects
    // in the system
    auto planets = root["planets"];
    for(unsigned int i = 0; i < planets.size(); i ++){
        if(!usePlanet(planets[i]["name"].asString()))
            continue;

        sys.add(planets[i]["name"].asString(),
                planets[i]["mass"].asDouble(),
                vec3(planets[i]["position"][0].asDouble(),
                     planets[i]["position"][1].asDouble(),
                     planets[i]["position"][2].asDouble()),
                vec3(planets[i]["velocity"][0].asDouble(),
                     planets[i]["velocity"][1].asDouble(),
                     planets[i]["velocity"][2].asDouble()),
                saveableSteps);
    }
    if(sys.planets.size() <= 0)
        throw std::runtime_error("No planets found during initialization");

    std::string using_planets = "Using planets ";
    for(auto &planet: sys.planets){
        using_planets += planet->name + ", ";
        planet->beta = gravitationalExponent;
        if(relativisticCorrection)
            planet->setForce(Force::RELATIVISTIC);
        else
            planet->setForce(Force::NEWTONIAN);
    }
    std::cout << using_planets << std::endl;

    // Set up the energy array with the format
    // time - kinetic energy - potential energy
    energyArray = arma::zeros(3, saveableSteps+1);
    angMomArray = arma::zeros(saveableSteps+1);
    sys.sort();
}

bool Solver::usePlanet(const std::string& planet_name) const{
    // Checks in input if a planet is to be used in the simulation by name
    if(use_all_planets)
        return true;
    for(unsigned int i = 0; i < planets_to_use.size(); i++){
        if(planet_name == planets_to_use[i].asString())
            return true;
    }
    return false;
}

bool Solver::doSaveStep(unsigned int currentStep){
    if(!saveAnyResults) return false;
    // Memonize the function
    if(currentStep == previousStep) return previousAnswer;

    if(saveFlag){
        saveStep = currentStep;
        previousAnswer = true;
        previousStep = currentStep;
        return true;
    } else if ((currentStep % savePeriod) == 0){
        saveStep = currentStep / savePeriod;
        if (saveStep <= saveableSteps){
            previousAnswer = true;
            previousStep = currentStep;
            return true;
        }
    }
    previousStep = currentStep;
    previousAnswer = false;
    return false;
}

void Solver::saveToFile(){
    // Save the position
    std::ofstream positionstream;
    positionstream.open(savepath + "/position.txt");
    // Loop over time points
    for(unsigned int i = 0; i < saveableSteps; i++){
        // Loop over planets
        unsigned int j = 0;
        for(auto & planet: sys.planets){
            positionstream << planet->pos_array(0, i) << " ";
            positionstream << planet->pos_array(1, i) << " ";
            positionstream << planet->pos_array(2, i) << " ";
            if(j == sys.n_planets-1){
                positionstream << "\n";}
            j++;
        }
    }
    positionstream.close();

    // Save the energy and angular momentum
    energyArray.save(savepath + "/energy.txt", arma::raw_ascii);
    angMomArray.save(savepath + "/angmom.txt", arma::raw_ascii);
    // Save perihelion time points and angles
    if(findPerihelionPrecession){
        std::ofstream thetastream;
        thetastream.open(savepath + "/precession.txt");
        for(auto& timenangle: perihelions){
            thetastream << std::setprecision(15) << timenangle.first << " " << timenangle.second << '\n';
        }
        thetastream.close();
    }
}

void Solver::startTiming(){
  // Start timing the algorithm (both wall time and cpu time)
  startWallTime = std::chrono::high_resolution_clock::now();
  startCPUTime  = std::clock();
}

void Solver::endTiming(){
  // Print wall time and cpu time when called
  auto CPUTime = (std::clock() - startCPUTime)/static_cast<double>(CLOCKS_PER_SEC);
  std::chrono::duration<double> wallTime = (std::chrono::high_resolution_clock::now() - startWallTime);
  std::cout << "Wall time: " << wallTime.count() << "s\n"
            << "CPU time: " << CPUTime << "s" << std::endl;
}
