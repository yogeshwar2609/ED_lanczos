/* Exact Diagonalization Lanczos version*/
/* In this state the convention is defined as c1_u^dagger*c2_u^dagger*c3_u^dagger...c1_d^dagger*c2d^dagger...|0>*/
/* Using this convention the hopping term is all postive for nearest neighbor*/
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <ctime>
#include "matrix.h"
#include "myutil.h"
#include "random.h"
#include "diag.h"
#include <omp.h>
#include <cstdlib>
#include <cassert>

using namespace std;

typedef enum
{
	anderson = 0, hubbard = 1
} Model;

typedef struct Parameters
{
    int N;// length of the chain
    int omegaPoints;// gridpoints for the frequency of A(omega) and G(omega)
    double u;// Coulomb interaction
    double eps;// on-site energy
    double t;// hopping strength
    double broadening;// broadening parameter for the delta-peaks
    double bandWidth;// range for the frequency
    Model model;// defines the model (either U_i = U*delta_{i,1} or U_i = U)
    int itmax;// maximum lanczos iterations
} Parameters;

typedef map<int, map<int, int> > QSzCount;// total number, Sz, number of state
//typedef map<int, map<int, vector<double> > > Energies;// total number, Sz, energies
typedef map<int, map<int, Matrix> > States;// total number, Sz, eigenstates
//typedef map<int, map<int, vector<double> > > States; // storing the states 
typedef map<int, map<int, vector<vector<int> > > > Basis;
// total number, Sz, basis |-1,0,2,1,1.....> as occupation -1 spin dn,1 spin up, 0 empty, 2 double occupied
//typedef vector<map<int, map<int, Matrix> > > Lanczos_States;// storing all the lanczos states

void buildbasis(const int N, vector<int> &s, Basis &basis, QSzCount &qszcount, map<int, int> &countSubspaces, ofstream &info );
bool newConfiguration(vector<int> &s, int lower, int upper);// generate new configuration from lower(-1) spin dn to upper(2) double occupied
void lanczos( Parameters parameters, Basis &basis, QSzCount &qszcount, vector<double> &a , vector<double> &b ,vector<double> &energies, vector<double> &gs_lanczos_coeff ,ofstream &info );
States lanczos_gs_wavefn(Parameters parameters, Basis &basis, QSzCount &qszcount, vector<double> &energies, vector<double> &gs_lanczos_coeff ,ofstream &info );
States H_dot_u( Parameters parameters, Basis &basis, QSzCount &qszcount, States &u );// H|u>
double u_dot_up( Parameters parameters, Basis &basis, QSzCount &qszcount, States &u, States &up );// <u|up> u dot uprime
vector<double> diagonalize_tri(vector<double> diag , vector<double> offdiag , vector<double> &gs_lanczos_coeff );// diagonalize tridigonal matrix
void broadeningPoles(const vector<double> &poles, const vector<double> &weights, vector<double> &newGrid, vector<double> &smoothFunction, Parameters &p);
void kramersKronig(const vector<double> &x, const vector<double> &fin, vector<double> &fout, Parameters &p);
double gaussian(double omega, double b);

int main(int argc, const char* argv[])
{
    int N=6, itmax=200 , charge , spin;
    //RanGSL rand(1234);//initial random number generator with seed
    Parameters parameters = {N, 5000, 2., -1., -0.5, 0.1, 10, hubbard/*anderson*/, itmax};

    if (argc==1) {
       cout << "commandline argument: N, U, mu, t, broaden, model(0 for anderson or 1 for hubbard), itmax " <<endl;
       return 1;
    }

    // reads in command line arguments, it is not essential
    switch (argc)
    {
        case 8:
            parameters.itmax =atoi(argv[7]);
        case 7:
            //parameters.model = (Model) atoi(argv[6]);
            parameters.model = static_cast<Model> (atoi( argv[6]) );
        case 6:
            parameters.broadening = atof(argv[5]);
        case 5:
            parameters.t = atof(argv[4]);
        case 4:
            parameters.eps = atof(argv[3]);
        case 3:
            parameters.u = atof(argv[2]);
        case 2:
            parameters.N = atoi(argv[1]);
    }

    if(parameters.model == anderson)
      clog << "start" << parameters.N << "-site Anderson chain:" << endl;
    else
      clog << "start" << parameters.N << "-site Hubbard chain:" << endl;

    time_t start, end;
    time(&start);
    ofstream info("info.dat");// print out the runtime information
    vector<double> energies;// storing energies
    //States u_n, u_n_p1, u_n_m1;//storing temporary lanczos basis
    States gs_wavefn;// storing ground state wave function
    //States states;// storing eigevstates, as a orthonormal matrix
    // s[i] labels a state at site i: s[i] = 0 means empty state,
    // -1 means spin down state, 1 means spin up state and 2 means doubly occupied state
    vector<int> s(parameters.N, -1);// initialize as all spin down
    //Matrix hamiltonian;// storing temporary hamiltonian for diagonalization
    // qszcount counts the number of subspaces in quantum numbers Q, Sz
    QSzCount qszcount;//count the size of subspace for each block
    Basis basis;// storing the basis for eahc block
    //Lanczos_States lanczos_states; // storing lanczos basis for constructing ground state wavefunction
    map<int, int> countSubspaces;// storing the number of subspace which has the same size. (first integer size, second interger occur times)
    vector<double> a, b; // storing the diagonal elements a and off diagonal elements b for tridiagonal matrix
    vector<double> gs_lanczos_coeff; // storing ground state lanczos coefficients

    info << parameters.N << "-site chain\n" << endl;
    if (parameters.model == anderson)
        info << "tight-binding with interaction on first site(Anderson) only\n" << endl;
    else
        info << "Hubbard model\n" << endl;
    info << "U = " << parameters.u << "\neps = " << parameters.eps << "\nt = ";
    info << parameters.t << "\n" << endl;

    // initialize the size of space for each block as 0 
    for (int q = -parameters.N; q <= parameters.N; q++)// the real charge should plus N 
        for (int sz = -parameters.N; sz <= parameters.N; sz++)
            qszcount[q][sz] = 0;
    
    // Set up the basis
    buildbasis(parameters.N, s, basis, qszcount, countSubspaces, info );
    // first lanczos to get ground state lanczos coefficient
    lanczos( parameters, basis, qszcount, a, b, energies, gs_lanczos_coeff, info );
    // calculating ground state wave function by retracing lacnzos basis |u_n>
    gs_wavefn = lanczos_gs_wavefn(parameters, basis, qszcount, energies, gs_lanczos_coeff ,info );

    //check g.s. lanczos coefficient in lanczos basis
/*    double sum =0;
    for (int i=0; i<gs_lanczos_coeff.size(); i++) {
        sum += pow(gs_lanczos_coeff[i],2);
        clog << gs_lanczos_coeff[i] << endl;
    }
    clog << "sum of square of lanczos coefficeint:" << sum << endl;
*/
    //check g.s. wavefunction in original basis
    double sum =0;
    for (int q = -parameters.N; q <= parameters.N; q++)
    {
        for (int sz = -parameters.N; sz <= parameters.N; sz++)
        {
            if ( qszcount[q][sz]==0 ) continue;
            for (int r=0; r< qszcount[q][sz]; r++) {
                sum += pow(gs_wavefn[q][sz].get(r,0),2);
                cout << gs_wavefn[q][sz].get(r,0) << endl;
            }
        }
    }
    clog << "sum of square of ground state coefficeint:" << sum << endl;

    return 0;
}

void buildbasis(int N,  vector<int> &s, Basis &basis, QSzCount &qszcount, map<int, int> &countSubspaces, ofstream &info )
{
    bool hasNext = true;
    for (;;)
    {
        int charge = 0;
        int spin = 0;
        // loop over all sites for looking the Q and Sz, start from state
        for (unsigned int i = 0; i < s.size(); i++)
        {
            // Calculate spin and charge of each configuration...
            charge += abs(s[i])-1;
            spin += s[i]*(2-abs(s[i]));// the term *(2-abs(s[i])) is set for double occupied state, i.e. spin=0.
        }
        // and store the configuration in the corresponding Q, Sz subspace
        basis[charge][spin].push_back(s);
        qszcount[charge][spin]++;
        // leave the loop if there exists no further basis state
        if (!hasNext)
            break;
        // determine next configuration and if there exists another one after that
        hasNext = newConfiguration(s, -1, 2);
    }

    // find and print largest subspace
    int largestSubspace = 0;
    for (int q = -N; q <= N; q++)
        for (int sz = -N; sz <= N; sz++)
        {
            if (qszcount[q][sz] > largestSubspace)
                largestSubspace = qszcount[q][sz];
            countSubspaces[qszcount[q][sz]]++;
        }

    // count number and sizes of subspaces
    for (map<int, int>::iterator it = countSubspaces.begin(); it != countSubspaces.end(); it++)
        info << (*it).second/*number of countSubspaces*/ << " subspaces of size " << (*it).first/*subsoace size (key qszcount)*/ << endl;
    info << "\nLargest subspace = " << largestSubspace << endl << endl;

}

bool newConfiguration(vector<int> &s, int lower, int upper)
{
    for (unsigned int i = 0; i < s.size(); i++)
    {
        if (s[i] < upper)
        {
            // increase one state, then leave the loop
            s[i]++;
            break;
        } else
            s[i] = lower;
    }
    // if there is any state not doubly occupied, we have some more
    // states to build and return true, ... 
    for (unsigned int i = 0; i < s.size(); i++)
        if (s[i] != upper)
            return true;
    // ... else we return false
    return false;
}

double u_dot_up( Parameters parameters, Basis &basis, QSzCount &qszcount, States &u, States &up )
{
    double sum=0;
    for (int q = -parameters.N; q <= parameters.N; q++)
    {
        for (int sz = -parameters.N; sz <= parameters.N; sz++)
        {
            if (qszcount[q][sz] == 0) continue;
            sum += (u[q][sz].returnTransposed()*up[q][sz]).get(0,0);
        }
    }
    //cout << sum << endl;
    return sum;
};

States H_dot_u( Parameters parameters, Basis &basis, QSzCount &qszcount, States &u )
{
    clog<<"enter H_dot_u"<<endl;
    //Matrix hamiltonian;//storing temporary partial hamiltonian for q and sz
    Matrix Hu_q_sz;
    States Hu;// storing H_dot_u

    //#pragma omp parallel for
    for (int q = -parameters.N; q <= parameters.N; q++)
    {
        for (int sz = -parameters.N; sz <= parameters.N; sz++)
        {
            if (qszcount[q][sz] == 0)
                continue;

            cout << "calculating H|u> : Q = " << q << ", Sz = " << sz << ", subspace size: " << qszcount[q][sz] << endl;
            // set up Hamiltonian in this subspace:
            //hamiltonian.resize(qszcount[q][sz], qszcount[q][sz]);
            //hamiltonian.zero();
            Hu_q_sz.resize(qszcount[q][sz],1);
            Hu[q][sz].resize(qszcount[q][sz],1);
            //set up u_q_sz
     
            #pragma omp parallel for
            for (int r = 0; r < qszcount[q][sz]; r++) // ket state
            {
                double sum=0;
                if (parameters.model == anderson)
                {
                    // Problem 2 a):
                    // =============
                    // Fill in the diagonal matrix elements of the Hamiltonian
                    // for the model with only first-site(Anderson) interaction
                    double temp=0; //temperory memory for diagonal element
                    //cout << basis[q][sz][r][m] << endl;
                    if ((basis[q][sz][r][0] == -1) )
                          temp += parameters.eps;
                    if ((basis[q][sz][r][0] ==  1) )
                          temp += parameters.eps;
                    if ((basis[q][sz][r][0] ==  0) )
                          temp =  temp;
                    if ((basis[q][sz][r][0] == 2) )
                          temp += 2*parameters.eps+parameters.u;

                    //hamiltonian.set(r,r, temp);
                    sum+=temp*u[q][sz].get(r,0);
                }
                else
                {
                    // Problem 2 b):
                    // =============
                    // Fill in the diagonal matrix elements of the Hamiltonian
                    // for the Hubbard model
                    double temp=0; //temperory memory for diagonal element
                    for(int m=0; m<parameters.N; m++)
                    { 
                      //cout << basis[q][sz][r][m] << endl;
                      if ((basis[q][sz][r][m] == -1) )
                            temp += parameters.eps;
                      if ((basis[q][sz][r][m] ==  1) )
                            temp += parameters.eps;
                      if ((basis[q][sz][r][m] ==  0) )
                            temp =  temp;
                      if ((basis[q][sz][r][m] == 2) )
                            temp += 2*parameters.eps+parameters.u;

                    }
                    //hamiltonian.set(r,r, temp);
                    sum+=temp*u[q][sz].get(r,0);
                    //cout << "diag element=" << hamiltonian.get(r,r) <<endl;
                                        
                }
                
                // hopping between sites:
                //#pragma omp parallel for shared(sum, hamiltonian,u)
                for (int rp = 0; rp < qszcount[q][sz]; rp++) // bra state
                {
                    for (int m = 0; m < parameters.N-1; m++)// searching hoping term from the basis
                    {
                        bool p = false;
                        for (int mp = 0; mp < parameters.N; mp++)
                        {
                            // if anything but two neighbouring sites...
                            if ((mp == m) || (mp == m+1))
                                continue;
                            // ... are different from each other... 
                            if (basis[q][sz][r][mp] != basis[q][sz][rp][mp])
                                p = true;
                        }//mp
                        // ... then there couldn't be a non-vanishing matrix element in the Hamiltonian
                        if (p)
                            continue;
                        
                        // Problem 2 c):
                        // ==========
                        // In the following, fill in all the missing matrix elements
                        
                        if ((basis[q][sz][r][m] == 0) && (basis[q][sz][r][m+1] == 1) && (basis[q][sz][rp][m] == 1) && (basis[q][sz][rp][m+1] == 0))
                            //hamiltonian.set(r, rp, parameters.t);
                            sum+=parameters.t*u[q][sz].get(rp,0);
                        if ((basis[q][sz][r][m] == -1) && (basis[q][sz][r][m+1] == 1) && (basis[q][sz][rp][m] == 2) && (basis[q][sz][rp][m+1] == 0))
                            //hamiltonian.set(r, rp, parameters.t);
                            sum+=parameters.t*u[q][sz].get(rp,0);
                        if ((basis[q][sz][r][m] == 0) && (basis[q][sz][r][m+1] == -1) && (basis[q][sz][rp][m] == -1) && (basis[q][sz][rp][m+1] == 0))
                            //hamiltonian.set(r, rp, parameters.t);
                            sum+=parameters.t*u[q][sz].get(rp,0);
                        if ((basis[q][sz][r][m] == 1) && (basis[q][sz][r][m+1] == -1) && (basis[q][sz][rp][m] == 2) && (basis[q][sz][rp][m+1] == 0))
                            //hamiltonian.set(r, rp, parameters.t);
                            sum+=parameters.t*u[q][sz].get(rp,0);
                        if ((basis[q][sz][r][m] == 0) && (basis[q][sz][r][m+1] == 2) && (basis[q][sz][rp][m] == 1) && (basis[q][sz][rp][m+1] == -1))
                            //hamiltonian.set(r, rp, parameters.t);
                            sum+=parameters.t*u[q][sz].get(rp,0);
                        if ((basis[q][sz][r][m] == -1) && (basis[q][sz][r][m+1] == 2) && (basis[q][sz][rp][m] == 2) && (basis[q][sz][rp][m+1] == -1))
                            //hamiltonian.set(r, rp, parameters.t);
                            sum+=parameters.t*u[q][sz].get(rp,0);
                        if ((basis[q][sz][r][m] == 0) && (basis[q][sz][r][m+1] == 2) &&(basis[q][sz][rp][m] == -1) && (basis[q][sz][rp][m+1] == 1))
                            //hamiltonian.set(r, rp, parameters.t);
                            sum+=parameters.t*u[q][sz].get(rp,0);
                        if ((basis[q][sz][r][m] == 1) && (basis[q][sz][r][m+1] == 2) && (basis[q][sz][rp][m] == 2) && (basis[q][sz][rp][m+1] == 1))
                            //hamiltonian.set(r, rp, parameters.t);
                            sum+=parameters.t*u[q][sz].get(rp,0);
                        if ((basis[q][sz][r][m] == 1) && (basis[q][sz][r][m+1] == 0) &&(basis[q][sz][rp][m] == 0) && (basis[q][sz][rp][m+1] == 1))
                            //hamiltonian.set(r, rp, parameters.t);
                            sum+=parameters.t*u[q][sz].get(rp,0);
                        if ((basis[q][sz][r][m] == 1) && (basis[q][sz][r][m+1] == -1) &&(basis[q][sz][rp][m] == 0) && (basis[q][sz][rp][m+1] == 2))
                            //hamiltonian.set(r, rp, parameters.t);
                            sum+=parameters.t*u[q][sz].get(rp,0);
                        if ((basis[q][sz][r][m] == -1) && (basis[q][sz][r][m+1] == 0) &&(basis[q][sz][rp][m] == 0) && (basis[q][sz][rp][m+1] == -1))
                            //hamiltonian.set(r, rp, parameters.t);
                            sum+=parameters.t*u[q][sz].get(rp,0);
                        if ((basis[q][sz][r][m] == -1) && (basis[q][sz][r][m+1] == 1) &&(basis[q][sz][rp][m] == 0) && (basis[q][sz][rp][m+1] == 2))
                            //hamiltonian.set(r, rp, parameters.t);
                            sum+=parameters.t*u[q][sz].get(rp,0);
                        if ((basis[q][sz][r][m] == 2) && (basis[q][sz][r][m+1] == 0) &&(basis[q][sz][rp][m] == -1) && (basis[q][sz][rp][m+1] == 1))
                            //hamiltonian.set(r, rp, parameters.t);
                            sum+=parameters.t*u[q][sz].get(rp,0);
                        if ((basis[q][sz][r][m] == 2) && (basis[q][sz][r][m+1] == -1) && (basis[q][sz][rp][m] == -1) && (basis[q][sz][rp][m+1] == 2))
                            //hamiltonian.set(r, rp, parameters.t);
                            sum+=parameters.t*u[q][sz].get(rp,0);
                        if ((basis[q][sz][r][m] == 2) && (basis[q][sz][r][m+1] == 0) && (basis[q][sz][rp][m] == 1) && (basis[q][sz][rp][m+1] == -1))
                            //hamiltonian.set(r, rp, parameters.t);
                            sum+=parameters.t*u[q][sz].get(rp,0);
                        if ((basis[q][sz][r][m] == 2) && (basis[q][sz][r][m+1] == 1) && (basis[q][sz][rp][m] == 1) && (basis[q][sz][rp][m+1] == 2))
                            //hamiltonian.set(r, rp, parameters.t);
                            sum+=parameters.t*u[q][sz].get(rp,0);
                    }//m
                    //sum+=hamiltonian.get(r,rp)*u[q][sz].get(rp,0);
                }//rp
                Hu[q][sz].set(r,0,sum);
            }//r

            Hu_q_sz.erase();

        }//sz
    }//q
    return Hu;
}

void broadeningPoles(const vector<double> &poles, const vector<double> &weights, vector<double> &newGrid, vector<double> &smoothFunction, Parameters &p)
{
    double stepWidth = 2*p.bandWidth*p.t/p.omegaPoints;
    // set up the grid for the frequency values
    newGrid.resize(p.omegaPoints);
    smoothFunction.resize(p.omegaPoints);
    for (int i = 0; i < p.omegaPoints; i++)
        newGrid[i] = -p.bandWidth*p.t + i*stepWidth;
    
    // summation of contributions of all gaussians times their weight at each frequency
    for (int i = 0; i < p.omegaPoints; i++)
    {
        smoothFunction[i] = 0;
        for (unsigned int j = 0; j < poles.size(); j++)
        {
            smoothFunction[i] += weights[j] * gaussian(newGrid[i]-poles[j], p.broadening);
        }
    }
}

double gaussian(double omega, double b)
{
    b = 1/b;
    return b*exp(-omega*omega*b*b)/sqrt(M_PI);
}


void kramersKronig(const vector<double> &x, const vector<double> &fin, vector<double> &fout, Parameters &p)
{
    /* This function calculates the Kramers-Kronig-Transform of the function fin
     * using the trapezian integration method and returns the result in function
     * fout.
     * Kramers Kronig int_{-/infty}^/infty fin(y)/(x-y)dy
     * In order not to divide by zero -> i != j.
     */
    
    double deltax = x[1] - x[0];
    for (int i = 0; i < p.omegaPoints; i++)
    {
        if (i != 0 && i != p.omegaPoints-1)
            fout[i] = 0.5*(fin[0]/(x[0]-x[i]) + fin[p.omegaPoints-1]/(x[p.omegaPoints-1]-x[i]));
        for (int j = 1; j < p.omegaPoints-1; j++)
        {
            if (i != j)
                fout[i] += fin[j]/(x[j]-x[i]);
        }
        fout[i] *= -deltax/M_PI;
    }
}

void lanczos( Parameters parameters, Basis &basis, QSzCount &qszcount , vector<double> &a, vector<double> &b, vector<double> &energies, vector<double> &gs_lanczos_coeff , ofstream &info )
{
    RanGSL rand(1234);//initial random number generator with seed
    States u_n, u_n_p1, u_n_m1;//storing temporary lanczos basis

    char str[40];
    if(parameters.model==anderson)
      sprintf(str,"energies_anderson_N%dU%3.2f.dat",parameters.N,parameters.u);
    else
      sprintf(str,"energies_hubbard_N%dU%3.2f.dat",parameters.N,parameters.u);
    ofstream energiesOfStream(str);

    clog << "iteration=0:" << endl;
    // Set up the initial random Lanczos state u0    
    int hil_count = 0;
    for (int q = -parameters.N; q <= parameters.N; q++)
    {
        for (int sz = -parameters.N; sz <= parameters.N; sz++)
        {
            if ( qszcount[q][sz]==0 ) continue;
            u_n[q][sz].resize(qszcount[q][sz],1);
            for (int r=0; r< qszcount[q][sz]; r++) {
                u_n[q][sz].set(r,0,rand());
                //cout << u_n[q][sz].get(r,0) << endl;
                hil_count+=1;
            }
        }
    }
    assert (pow(4,parameters.N)==hil_count);
    clog << "total size of hilbert space:" << hil_count << endl;

    States Hu_n=H_dot_u( parameters, basis, qszcount, u_n ); 
    //calculate a_n=<u_n|H|u_n>/<u_n|u_n>
    a.push_back( u_dot_up( parameters, basis, qszcount, u_n, Hu_n) / u_dot_up( parameters, basis, qszcount, u_n, u_n) );
    for (int q = -parameters.N; q <= parameters.N; q++)
    {
        for (int sz = -parameters.N; sz <= parameters.N; sz++)
        { 
            if (qszcount[q][sz]==0) continue; 
            Matrix a_u_n_q_sz = u_n[q][sz];
            a_u_n_q_sz.multiply( a[0] );
            u_n_p1[q][sz] = Hu_n[q][sz] - a_u_n_q_sz  ;
        }
    }

    clog << "iteration=1:" << endl;
    for (int q = -parameters.N; q <= parameters.N; q++)
    {
        for (int sz = -parameters.N; sz <= parameters.N; sz++)
        {
            if (qszcount[q][sz]==0) continue;
            u_n_m1[q][sz] = u_n[q][sz];
            u_n[q][sz] = u_n_p1[q][sz];
        }
    }
    Hu_n=H_dot_u( parameters, basis, qszcount, u_n );
    //calculate a_n=<u_n|H|u_n>/<u_n|u_n>
    a.push_back( u_dot_up( parameters, basis, qszcount, u_n, Hu_n) / u_dot_up( parameters, basis, qszcount, u_n, u_n) );
    b.push_back( sqrt( u_dot_up( parameters, basis, qszcount, u_n, u_n) / u_dot_up( parameters, basis, qszcount, u_n_m1, u_n_m1) ) );
    for (int q = -parameters.N; q <= parameters.N; q++)
    {
        for (int sz = -parameters.N; sz <= parameters.N; sz++)
        {
            if (qszcount[q][sz]==0) continue;
            Matrix a_u_n_q_sz = u_n[q][sz];
            Matrix b_u_nm1_q_sz = u_n_m1[q][sz];
            a_u_n_q_sz.multiply( a[1] );
            b_u_nm1_q_sz.multiply( pow(b[0], 2) );
            
            u_n_p1[q][sz] = Hu_n[q][sz] - a_u_n_q_sz - b_u_nm1_q_sz ;
        }
    }

    energies = diagonalize_tri( a, b, gs_lanczos_coeff );
    clog << "g.s. energy:" << energies[0] << endl;
    
    for (int it=2; it<parameters.itmax; it++)
    {
        double en, enm1=energies[0], de=pow(10,9);

        gs_lanczos_coeff.clear();
        clog << "iteration=" << it << ":" <<endl;
        for (int q = -parameters.N; q <= parameters.N; q++)
        {
            for (int sz = -parameters.N; sz <= parameters.N; sz++)
            {
                if (qszcount[q][sz]==0) continue;
                u_n_m1[q][sz] = u_n[q][sz];
                u_n[q][sz] = u_n_p1[q][sz];
            }
        }
        Hu_n=H_dot_u( parameters, basis, qszcount, u_n );
        //calculate a_n=<u_n|H|u_n>/<u_n|u_n>
        a.push_back( u_dot_up( parameters, basis, qszcount, u_n, Hu_n) / u_dot_up( parameters, basis, qszcount, u_n, u_n) );
        b.push_back( sqrt( u_dot_up( parameters, basis, qszcount, u_n, u_n) / u_dot_up( parameters, basis, qszcount, u_n_m1, u_n_m1) ) );
        for (int q = -parameters.N; q <= parameters.N; q++)
        {
            for (int sz = -parameters.N; sz <= parameters.N; sz++)
            {
                if (qszcount[q][sz]==0) continue;
                Matrix a_u_n_q_sz = u_n[q][sz];
                Matrix b_u_nm1_q_sz = u_n_m1[q][sz];
                a_u_n_q_sz.multiply( a[it] );
                b_u_nm1_q_sz.multiply( pow(b[it-1], 2) );
                u_n_p1[q][sz] = Hu_n[q][sz] - a_u_n_q_sz - b_u_nm1_q_sz ;
            }
        }
        energies = diagonalize_tri( a, b, gs_lanczos_coeff );

        en = energies[0];
        de = abs( en - enm1 );
        clog << "g.s. energy:" << energies[0] << "  dE_gs= " << de << endl;

        // output energy
        if ( it > 3 )  energiesOfStream << it << "\t" << energies[0]<< "\t" << energies[1]<< "\t" << energies[2] << "\t" << energies[3] << endl;

        // check convergence
        if(de < pow(10,-14)) break;
        else enm1 = en;
    }

    energiesOfStream.close();
    info << "ground state energy:" << endl;
    info << "energy = " << energies[0] << endl;    

}

States lanczos_gs_wavefn(Parameters parameters, Basis &basis, QSzCount &qszcount, vector<double> &energies, vector<double> &gs_lanczos_coeff ,ofstream &info )
{
    RanGSL rand(1234);//initial random number generator with seed
    States u_n, u_n_p1, u_n_m1;// storing temporary lanczos basis
    States gs_wavefn;// storing the ground state wavefunction 
    vector<double> a, b;// storing the diagonal elements a and off diagonal elements b for tridiagonal matrix
    vector<double> c; //storing useless gs_lanczos_coeff
    double norm;//normalization factor for wavefunction (Note the tridiagonal matrix is in normalized lanczos basis)

    clog << "iteration=0:" << endl;
    // Set up the initial random Lanczos state u0    
    int hil_count = 0;
    for (int q = -parameters.N; q <= parameters.N; q++)
    {
        for (int sz = -parameters.N; sz <= parameters.N; sz++)
        {
            if ( qszcount[q][sz]==0 ) continue;
            gs_wavefn[q][sz].resize(qszcount[q][sz],1);
            u_n[q][sz].resize(qszcount[q][sz],1);
            for (int r=0; r< qszcount[q][sz]; r++) {
                u_n[q][sz].set(r,0,rand());
                //cout << u_n[q][sz].get(r,0) << endl;
                hil_count+=1;
            }
        }
    }
    assert (pow(4,parameters.N)==hil_count);
    clog << "total size of hilbert space:" << hil_count << endl;

    norm=sqrt( u_dot_up( parameters, basis, qszcount, u_n, u_n ));
    States Hu_n=H_dot_u( parameters, basis, qszcount, u_n ); 
    //calculate a_n=<u_n|H|u_n>/<u_n|u_n>
    a.push_back( u_dot_up( parameters, basis, qszcount, u_n, Hu_n) / pow(norm,2) );
    for (int q = -parameters.N; q <= parameters.N; q++)
    {
        for (int sz = -parameters.N; sz <= parameters.N; sz++)
        { 
            if (qszcount[q][sz]==0) continue;
            for ( int r=0; r< qszcount[q][sz]; r++)
            {
                double temp =gs_lanczos_coeff[0]*u_n[q][sz].get(r,0)/norm;
                gs_wavefn[q][sz].set(r,0,temp);
            }
            Matrix a_u_n_q_sz = u_n[q][sz];
            a_u_n_q_sz.multiply( a[0] );
            u_n_p1[q][sz] = Hu_n[q][sz] - a_u_n_q_sz  ;
        }
    }

    clog << "iteration=1:" << endl;
    for (int q = -parameters.N; q <= parameters.N; q++)
    {
        for (int sz = -parameters.N; sz <= parameters.N; sz++)
        {
            if (qszcount[q][sz]==0) continue;
            u_n_m1[q][sz] = u_n[q][sz];
            u_n[q][sz] = u_n_p1[q][sz];
        }
    }
    norm=sqrt( u_dot_up( parameters, basis, qszcount, u_n, u_n ));
    Hu_n=H_dot_u( parameters, basis, qszcount, u_n );
    //calculate a_n=<u_n|H|u_n>/<u_n|u_n>
    a.push_back( u_dot_up( parameters, basis, qszcount, u_n, Hu_n) / pow(norm, 2) );
    b.push_back( sqrt( pow(norm,2) / u_dot_up( parameters, basis, qszcount, u_n_m1, u_n_m1) ) );
    for (int q = -parameters.N; q <= parameters.N; q++)
    {
        for (int sz = -parameters.N; sz <= parameters.N; sz++)
        {
            if (qszcount[q][sz]==0) continue;
            for ( int r=0; r< qszcount[q][sz]; r++) 
            {
                double temp = gs_wavefn[q][sz].get(r,0);
                temp += gs_lanczos_coeff[1]*u_n[q][sz].get(r,0)/norm;
                gs_wavefn[q][sz].set(r,0,temp);
            }
            Matrix a_u_n_q_sz = u_n[q][sz];
            Matrix b_u_nm1_q_sz = u_n_m1[q][sz];
            a_u_n_q_sz.multiply( a[1] );
            b_u_nm1_q_sz.multiply( pow(b[0], 2) );
            
            u_n_p1[q][sz] = Hu_n[q][sz] - a_u_n_q_sz - b_u_nm1_q_sz ;
        }
    }

    energies = diagonalize_tri( a, b, c );
    clog << "g.s. wavefunction calculation. g.s. energy:" << energies[0] << endl;
    
    for (int it=2; it<parameters.itmax; it++)
    {
        double en, enm1=energies[0], de=pow(10,9);

        gs_lanczos_coeff.clear();
        clog << "iteration=" << it << ":" <<endl;
        for (int q = -parameters.N; q <= parameters.N; q++)
        {
            for (int sz = -parameters.N; sz <= parameters.N; sz++)
            {
                if (qszcount[q][sz]==0) continue;
                u_n_m1[q][sz] = u_n[q][sz];
                u_n[q][sz] = u_n_p1[q][sz];
                
            }
        }
        norm=sqrt( u_dot_up( parameters, basis, qszcount, u_n, u_n ));
        Hu_n=H_dot_u( parameters, basis, qszcount, u_n );
        //calculate a_n=<u_n|H|u_n>/<u_n|u_n>
        a.push_back( u_dot_up( parameters, basis, qszcount, u_n, Hu_n) / pow(norm,2) );
        b.push_back( sqrt( pow(norm,2) / u_dot_up( parameters, basis, qszcount, u_n_m1, u_n_m1) ) );
        for (int q = -parameters.N; q <= parameters.N; q++)
        {
            for (int sz = -parameters.N; sz <= parameters.N; sz++)
            {
                if (qszcount[q][sz]==0) continue;
                for ( int r=0; r< qszcount[q][sz]; r++) 
                {
                    double temp = gs_wavefn[q][sz].get(r,0);
                    temp += gs_lanczos_coeff[it]*u_n[q][sz].get(r,0)/norm;
                    gs_wavefn[q][sz].set(r,0,temp);
                }
                Matrix a_u_n_q_sz = u_n[q][sz];
                Matrix b_u_nm1_q_sz = u_n_m1[q][sz];
                a_u_n_q_sz.multiply( a[it] );
                b_u_nm1_q_sz.multiply( pow(b[it-1], 2) );
                u_n_p1[q][sz] = Hu_n[q][sz] - a_u_n_q_sz - b_u_nm1_q_sz ;
            }
        }
        energies = diagonalize_tri( a, b, c );

        en = energies[0];
        de = abs( en - enm1 );
        clog << "g.s. wave function calculation. g.s. energy:" << energies[0] << "  dE_gs= " << de << endl;

        //check convergence
        if(de < pow(10,-14)) break;
        else enm1 = en;
    }

    info << "Calculating ground state wave function" << endl;

    return gs_wavefn;
}

vector<double> diagonalize_tri(vector<double> diag, vector<double> offdiag, vector<double> &gs_lanczos_coeff ) 
{

    int N = diag.size();
    //vector<double> eigenvalues(N);
    double eigenvectors[N*N];
    char v = 'V';
    int info, lwork = max(1, 3*N-1);
    double *work = NULL;

    work = new double[lwork];
    utils::dstev_(&v, &N, &diag.at(0), &offdiag.at(0), eigenvectors , &N ,work, &info);

    if (info != 0)
      cout << "diagonalization routine dsyev_ error: info = " << info << endl;Matrix c;

    for(int i=0; i<N; i++) gs_lanczos_coeff.push_back(eigenvectors[i]);//push_back ground state lanczos coefficient

    delete[] work;
    return diag;//eigenvalues;

    //cout << "The eigenvectors are:" << endl;
    //for(int i=0; i<N*N;i++) cout << eigenvectors[i] <<"\t";
    //cout<< endl;
}
