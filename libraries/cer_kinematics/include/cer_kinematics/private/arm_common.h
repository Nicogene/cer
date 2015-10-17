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


/****************************************************************/
class ArmCommonNLP : public Ipopt::TNLP
{
protected:
    const TripodParametersExtended torso;    
    const TripodParametersExtended lower_arm;
    iKinLimb &upper_arm;

    double drho;

    Matrix &T0,&TN;
    double &zd1,&zd2;
    double &wpostural_torso;
    double &wpostural_upper_arm;

    Matrix H0,HN,Rd;
    Vector x0,x;
    Vector xd,ud;

public:
    /****************************************************************/
    ArmCommonNLP(ArmParameters &pa, SolverParameters &ps) :
                 torso(pa.torso), upper_arm(pa.upper_arm), lower_arm(pa.lower_arm),
                 T0(pa.T0), TN(pa.TN), zd1(ps.torso_heave), zd2(ps.lower_arm_heave),
                 wpostural_torso(ps.weight_postural_torso),
                 wpostural_upper_arm(ps.weight_postural_upper_arm)
    {
        drho=DELTA_RHO;

        H0=upper_arm.getH0();
        HN=upper_arm.getHN();

        x0.resize(12,0.0);

        iKinChain *chain=upper_arm.asChain();
        for (size_t i=0; i<upper_arm.getDOF(); i++)
            x0[3+i]=0.5*((*chain)[i].getMin()+(*chain)[i].getMax());
        
        x=x0;
        set_target(eye(4,4));
    }

    /****************************************************************/
    virtual string get_mode() const=0;

    /****************************************************************/
    TripodState tripod_fkin(const int which, const Vector &x) const
    {
        return tripod_fkin(which,(Ipopt::Number*)x.data());
    }

    /****************************************************************/
    TripodState tripod_fkin(const int which, const Ipopt::Number *x) const
    {
        const TripodParametersExtended &params=((which==1)?torso:lower_arm);
        int offs=(which==1)?0:9;

        double q33=sqrt(27.0)*params.r/sqrt(12.0*(x[offs+2]*x[offs+2]-(x[offs+0]+x[offs+1])*x[offs+2]+
                                            x[offs+1]*x[offs+1]-x[offs+0]*x[offs+1]+x[offs+0]*x[offs+0]+
                                            (27.0/12.0)*params.r*params.r));

        TripodState d;
        if (q33>=1.0)
        {
            d.n=params.z;
            d.u=0.0;
            d.p[0]=d.p[1]=0.0;
            d.p[2]=d.T(2,3)=x[offs+0];
        }
        else
        {
            Vector v1=params.s[0]+x[offs+0]*params.z;
            Vector v2=params.s[1]+x[offs+1]*params.z;
            Vector v3=params.s[2]+x[offs+2]*params.z;
            d.n=cross(v2-v1,v3-v1);
            d.n/=norm(d.n);

            double sin_theta=sqrt(1.0-q33*q33);
            d.u[0]=-d.n[1]/sin_theta;
            d.u[1]=d.n[0]/sin_theta;
            d.u[2]=0.0;
            double tmp=(1.0-q33);
            double q11=tmp*d.u[0]*d.u[0]+q33;
            double q22=tmp*d.u[1]*d.u[1]+q33;
            double q21=tmp*d.u[0]*d.u[1];
            double q31=-sin_theta*d.u[1];
            double q32=sin_theta*d.u[0];
            double m1=params.r/q33*(-0.5*q11+1.5*q22);
            d.p[0]=params.r-m1*q11;
            d.p[1]=-m1*q21;
            d.p[2]=x[offs+0]-m1*q31;
            d.u*=acos(q33);

            // transformation matrix
            d.T(0,0)=q11; d.T(0,1)=q21; d.T(0,2)=-q31; d.T(0,3)=d.p[0];
            d.T(1,0)=q21; d.T(1,1)=q22; d.T(1,2)=-q32; d.T(1,3)=d.p[1];
            d.T(2,0)=q31; d.T(2,1)=q32; d.T(2,2)=q33;  d.T(2,3)=d.p[2];
        }

        return d;
    }

    /****************************************************************/
    Matrix fkin(const Vector &x)
    {
        TripodState d1=tripod_fkin(1,x);
        TripodState d2=tripod_fkin(2,x);

        return T0*d1.T*upper_arm.getH(CTRL_DEG2RAD*x.subVector(3,8))*d2.T*TN;
    }

    /****************************************************************/
    void set_q0(const Vector &x0)
    {
        this->x0[0]=std::max(torso.l_min,std::min(torso.l_max,x0[0]));
        this->x0[1]=std::max(torso.l_min,std::min(torso.l_max,x0[1]));
        this->x0[2]=std::max(torso.l_min,std::min(torso.l_max,x0[2]));

        iKinChain *chain=upper_arm.asChain();
        for (size_t i=0; i<upper_arm.getDOF(); i++)
            this->x0[3+i]=std::max((*chain)[i].getMin(),std::min((*chain)[i].getMax(),CTRL_DEG2RAD*x0[i]));

        this->x0[9]=std::max(lower_arm.l_min,std::min(lower_arm.l_max,x0[9]));
        this->x0[10]=std::max(lower_arm.l_min,std::min(lower_arm.l_max,x0[10]));
        this->x0[11]=std::max(lower_arm.l_min,std::min(lower_arm.l_max,x0[11]));
    }

    /****************************************************************/
    void set_target(const Matrix &Hd)
    {
        xd=Hd.getCol(3).subVector(0,2);

        Rd=Hd;
        Rd(0,3)=Rd(1,3)=Rd(2,3)=0.0;

        ud=dcm2axis(Rd);
        ud*=ud[3];
        ud.pop_back();
    }

    /****************************************************************/
    Vector get_result() const
    {
        Vector x_=x;
        for (size_t i=0; i<upper_arm.getDOF(); i++)
            x_[3+i]*=CTRL_RAD2DEG;

        return x_;
    }

    /****************************************************************/
    bool get_starting_point(Ipopt::Index n, bool init_x, Ipopt::Number *x,
                            bool init_z, Ipopt::Number *z_L, Ipopt::Number *z_U,
                            Ipopt::Index m, bool init_lambda, Ipopt::Number *lambda)
    {
        for (Ipopt::Index i=0; i<n; i++)
            x[i]=x0[i];

        return true;
    }

    /****************************************************************/
    bool eval_h(Ipopt::Index n, const Ipopt::Number *x, bool new_x,
                Ipopt::Number obj_factor, Ipopt::Index m, const Ipopt::Number *lambda,
                bool new_lambda, Ipopt::Index nele_hess, Ipopt::Index *iRow,
                Ipopt::Index *jCol, Ipopt::Number *values)
    {
        return true;
    }

    /****************************************************************/
    void finalize_solution(Ipopt::SolverReturn status, Ipopt::Index n,
                           const Ipopt::Number *x, const Ipopt::Number *z_L,
                           const Ipopt::Number *z_U, Ipopt::Index m,
                           const Ipopt::Number *g, const Ipopt::Number *lambda,
                           Ipopt::Number obj_value, const Ipopt::IpoptData *ip_data,
                           Ipopt::IpoptCalculatedQuantities *ip_cq)
    {
        for (Ipopt::Index i=0; i<n; i++)
            this->x[i]=x[i];
    }
};

