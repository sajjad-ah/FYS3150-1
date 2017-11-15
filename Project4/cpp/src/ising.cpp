#include <iostream>
#include <random>
#include <armadillo>
#include <array>
#include <random>
#include <string>
#include <chrono>
#include <thread>
#include "ising.h"
#include "metamodel.h"

int magnetization(arma::imat& A){
  // Return the magnetization of a state described by A, which is simply the sum
  // of the spins/elements. This function is somewhat redundant.
  return arma::accu(A);
}

arma::imat setInitialStateRandom(unsigned int N){
  // Generate armadillo integer matrix with uniform random negative or positive
  // ones.
  std::mt19937 gen;
  gen.seed(1233);
  std::uniform_real_distribution<double> dist(0.0,1.0);

  arma::imat state = arma::zeros<arma::imat>(N, N);
  for(unsigned int row = 0; row < N; row++){
    for(unsigned int col = 0; col < N; col++){
      if(dist(gen) < 0.5){
        state(row,col) = -1;
      }
      else{
        state(row,col) = 1;
      }
    }
  }
  return state;
}

arma::imat setInitialStateOrdered(unsigned int N, int fill){
  arma::imat state = arma::ones<arma::imat>(N, N);
  state.fill(fill);
  return state;
}

unsigned int period(int x, unsigned int N){
  unsigned int a = ((x+N)%N);
  if(a == 0){
    return 0;
  }
  else{
    return a;
  }
}

int sumNeighbors(unsigned int i, unsigned int j, arma::imat& A){
  unsigned int N = A.n_cols;
  int s = 0;
  s += A(period(i-1, N), j)  // Left neighbor
    +  A(i, period(j+1, N))  // Lower neighbor
    +  A(i, period(j-1, N))  // Upper neighbor
    +  A(period(i+1, N), j); // Right neighbor
  return s;
}

int totalEnergy(arma::imat& A){
  int E = 0;
  for(unsigned int i = 0; i < A.n_cols; i++){
    for(unsigned int j = 0; j < A.n_rows; j++){
      E -= A(i,j)*sumNeighbors(i,j,A);
    }
  }
  return E;
}

std::map<int,double> makeProbabilities(double beta){
  std::array<int, 5> dEStates{-4, -2, 0, 2, 4};
  std::map<int,double> probabilities;
  for(const auto& E: dEStates){
    probabilities[E] = exp(-beta*static_cast<double>(E));
  }
  return probabilities;
}


void ising(){
  metamodel model;
  int seed = model.seed;
  // Setup RNG generator
  std::mt19937 gen;
  gen.seed(seed);
  std::uniform_real_distribution<double> random(0.0,1.0);

  // Setup and run simulation of the Ising model.
  unsigned int N = model.N;      // Lattice size (N x N)
  unsigned int M = model.M;         // Number of MC-iterations
  double beta = 1.0/model.temperature;
  int m;
  int n;
  int dE;

  // Create probability map for possible energy changes
  auto probabilities = makeProbabilities(beta);

  // Initialize arrays and initial values
  arma::imat state = setInitialStateRandom(N);
  std::vector<arma::imat> states (M);
  states[0] = state; // Saves every state

  std::vector<double> energies (M);         // Save energy for each MC Cycle
  energies[0] = totalEnergy(state);

  std::vector<int> magmoments (M);          // Hold magnetization for each MC
  magmoments[0] = magnetization(state);

  // Loop through MC-cycles
  for(unsigned int i = 1; i < M; i++){
    // Choose row (m) and column (n) index to perturb/flip given acceptance
    m = static_cast<int>(random(gen)*N);
    n = static_cast<int>(random(gen)*N);
    // Calculate consequent change of energy
    dE = state(m,n)*sumNeighbors(m,n,state);
    // Metropolis algorithm
    if(dE < 0 && random(gen) < probabilities[dE]){
      state(m,n) = -state(m,n);
    }
    // Update states
    states[i] = state;
    energies[i] = energies[i-1] + dE;
    magmoments[i] = magmoments[i-1] + 2*state(m,n);
  }

  // Write energy to file
  std::ofstream energy;
  energy.open(model.basepath + model.energypath, std::ios::out | std::ios::binary);
  energy.write((char*)&energies[0], M*sizeof(energies[0]));
  energy.close();

  // Write magnetic moment to file
  std::ofstream magmom;
  magmom.open("../data/magneticmoment.bin", std::ios::out | std::ios::binary);
  magmom.write((char*)&magmoments[0], M*sizeof(magmoments[0]));
  magmom.close();

  writeMetaData(model);
}
