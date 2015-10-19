/*
 * Copyright (C) 2015 iCub Facility - Istituto Italiano di Tecnologia
 * Author: Ugo Pattacini
 * email:  ugo.pattacini@iit.it
 * Permission is granted to copy, distribute, and/or modify this program
 * under the terms of the GNU General Public License, version 2 or any
 * later version published by the Free Software Foundation.
 *
 * A copy of the license can be found at
 * http://www.robotcub.org/icub/license/gpl.txt
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details
*/

#include <cmath>
#include <algorithm>

#include <yarp/os/Log.h>
#include <yarp/os/Time.h>
#include <yarp/sig/Vector.h>
#include <yarp/sig/Matrix.h>
#include <yarp/math/Math.h>

#include <iCub/ctrl/math.h>
#include <iCub/iKin/iKinFwd.h>

#include <IpTNLP.hpp>
#include <IpIpoptApplication.hpp>

#include <cer_kinematics/arm.h>
#include <cer_kinematics/private/helpers.h>

// COMMON PART -- begin
#define DELTA_RHO       1e-6

using namespace std;
using namespace yarp::os;
using namespace yarp::sig;
using namespace yarp::math;
using namespace iCub::ctrl;
using namespace iCub::iKin;
using namespace cer_kinematics;
// COMMON PART -- end

namespace cer_kinematics {
    #include <cer_kinematics/private/arm_common.h>
    #include <cer_kinematics/private/arm_full.h>
    #include <cer_kinematics/private/arm_xyz.h>
    #include <cer_kinematics/private/arm_full_heave.h>
    #include <cer_kinematics/private/arm_xyz_heave.h>
}


/****************************************************************/
ArmSolver::ArmSolver(const ArmParameters &armParams,
                     const SolverParameters &slvParams,
                     const int verb) :
                     armParameters(armParams),
                     slvParameters(slvParams),
                     verbosity(verb)
{
}


/****************************************************************/
bool ArmSolver::setInitialGuess(const Vector &q0)
{
    size_t L=3+armParameters.upper_arm.getDOF()+3;
    if (q0.length()<L)
    {
        yError("mis-sized DOFs vector!");
        return false;
    }

    this->q0=q0.subVector(0,L-1);
    return true;
}


/****************************************************************/
bool ArmSolver::fkin(const Vector &q, Matrix &H, const int frame)
{
    size_t L=3+armParameters.upper_arm.getDOF()+3;
    if (q.length()<L)
    {
        yError("mis-sized DOFs vector!");
        return false;
    }

    Ipopt::SmartPtr<ArmFullNLP_CentralDiff> nlp=new ArmFullNLP_CentralDiff(armParameters,slvParameters);
    H=nlp->fkin(q,frame);

    return true;
}


/****************************************************************/
bool ArmSolver::ikin(const Matrix &Hd, Vector &q, int *exit_code)
{
    if ((Hd.rows()!=4) || (Hd.cols()!=4))
    {
        yError("mis-sized desired end-effector frame!");
        return false;
    }

    int print_level=std::max(verbosity-5,0);

    Ipopt::SmartPtr<Ipopt::IpoptApplication> app=new Ipopt::IpoptApplication;
    app->Options()->SetNumericValue("tol",slvParameters.tol);
    app->Options()->SetNumericValue("constr_viol_tol",slvParameters.constr_tol);
    app->Options()->SetIntegerValue("acceptable_iter",0);
    app->Options()->SetStringValue("mu_strategy","adaptive");
    app->Options()->SetIntegerValue("max_iter",2000);
    app->Options()->SetStringValue("nlp_scaling_method","gradient-based");
    app->Options()->SetStringValue("hessian_approximation","limited-memory");
    app->Options()->SetStringValue("derivative_test",print_level>4?"first-order":"none");
    app->Options()->SetIntegerValue("print_level",print_level);
    app->Initialize();

    Ipopt::SmartPtr<ArmCommonNLP> nlp;
    if (slvParameters.full_pose)
    {
        if (slvParameters.can_heave)
            if (slvParameters.enable_central_difference)
                nlp=new ArmFullHeaveNLP_CentralDiff(armParameters,slvParameters); 
            else
                nlp=new ArmFullHeaveNLP_ForwardDiff(armParameters,slvParameters); 
        else if (slvParameters.enable_central_difference)
            nlp=new ArmFullNLP_CentralDiff(armParameters,slvParameters);
        else
            nlp=new ArmFullNLP_ForwardDiff(armParameters,slvParameters);
    }
    else
    {
        if (slvParameters.can_heave)
            if (slvParameters.enable_central_difference)
                nlp=new ArmXyzHeaveNLP_CentralDiff(armParameters,slvParameters); 
            else
                nlp=new ArmXyzHeaveNLP_ForwardDiff(armParameters,slvParameters); 
        else if (slvParameters.enable_central_difference)
            nlp=new ArmXyzNLP_CentralDiff(armParameters,slvParameters);
        else
            nlp=new ArmXyzNLP_ForwardDiff(armParameters,slvParameters);
    }

    nlp->set_q0(q0);
    nlp->set_target(Hd);

    double t0=Time::now();
    Ipopt::ApplicationReturnStatus status=app->OptimizeTNLP(GetRawPtr(nlp));
    double t1=Time::now();    

    q=nlp->get_result();
    if (exit_code!=NULL)
        *exit_code=status;

    if (verbosity>0)
    {
        Vector xd=Hd.getCol(3).subVector(0,2);
        Vector ud=dcm2axis(Hd);
        ud*=ud[3];
        ud.pop_back();

        Matrix H=nlp->fkin(q);
        TripodState d1=nlp->tripod_fkin(1,q);
        TripodState d2=nlp->tripod_fkin(2,q);
        Vector x=H.getCol(3).subVector(0,2);
        Vector u=dcm2axis(H);
        u*=u[3];
        u.pop_back();

        yInfo(" *** Arm Solver ******************************");
        yInfo(" *** Arm Solver:          arm = %s",armParameters.upper_arm.getType().c_str());
        yInfo(" *** Arm Solver:         mode = %s",nlp->get_mode().c_str());
        yInfo(" *** Arm Solver:          tol = %g",slvParameters.tol);
        yInfo(" *** Arm Solver:   constr_tol = %g",slvParameters.constr_tol);
        yInfo(" *** Arm Solver:       q0 [*] = (%s)",q0.toString(3,3).c_str());
        yInfo(" *** Arm Solver:      zd1 [m] = %g",slvParameters.torso_heave);
        yInfo(" *** Arm Solver:      zd2 [m] = %g",slvParameters.lower_arm_heave);
        yInfo(" *** Arm Solver:       xd [m] = (%s)",xd.toString(3,3).c_str());
        yInfo(" *** Arm Solver:     ud [rad] = (%s)",ud.toString(3,3).c_str());
        yInfo(" *** Arm Solver:        q [*] = (%s)",q.toString(15,15).c_str());
        yInfo(" *** Arm Solver:      e_x [m] = %g",norm(xd-x));
        yInfo(" *** Arm Solver:    e_u [rad] = %g",norm(ud-u));
        yInfo(" *** Arm Solver:     e_z1 [m] = %g",fabs(slvParameters.torso_heave-d1.p[2]));
        yInfo(" *** Arm Solver: alpha1 [deg] = %g",CTRL_RAD2DEG*acos(d1.n[2]));
        yInfo(" *** Arm Solver:     e_z2 [m] = %g",fabs(slvParameters.lower_arm_heave-d2.p[2]));
        yInfo(" *** Arm Solver: alpha2 [deg] = %g",CTRL_RAD2DEG*acos(d2.n[2]));
        yInfo(" *** Arm Solver:      dt [ms] = %g",1000.0*(t1-t0));
        yInfo(" *** Arm Solver ******************************");
    }

    switch (status)
    {
        case Ipopt::Solve_Succeeded:
        case Ipopt::Solved_To_Acceptable_Level:
        case Ipopt::Feasible_Point_Found:
        {
            if (verbosity>0)
                yInfo(" *** Arm Solver: IpOpt return code %d",status);
            return true;
        } 

        default:
        {
            if (verbosity>0)
                yWarning(" *** Arm Solver: IpOpt return code %d",status);
            return false;
        } 
    }
}

