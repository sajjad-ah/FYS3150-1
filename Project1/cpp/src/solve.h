#ifndef SOLVE_H
#define SOLVE_H

#include <armadillo>

void solveGeneral(double (*func)(double),  unsigned int low,  unsigned int high,  unsigned int step,
                  double lowerBound, double upperBound);
void solveLU(double (*func)(double),  unsigned int low,  unsigned int high,  unsigned int step);
void solveLU(double (*func)(double),  unsigned int low,  unsigned int high,  unsigned int step,
             double lowerBound, double upperBound);
arma::mat tridiagonalMat(unsigned int size, double upper, double middle, double lower);
arma::vec thomas(const arma::vec& a, const arma::vec& b, const arma::vec& c, const arma::vec& d);


#endif /* SOLVE_H */
