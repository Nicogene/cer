/*
 * Copyright (C) 2013-2015 Fondazione Istituto Italiano di Tecnologia RBCS & iCub Facility & ADVR
 * Authors: see AUTHORS file.
 * CopyPolicy: Released under the terms of the LGPLv2.1 or any later version, see LGPL.TXT or LGPL3.TXT
 */

#include "GazeboYarpTripod.h"
#include "GazeboTripodMotionControl.h"
#include <GazeboYarpPlugins/common.h>
#include <GazeboYarpPlugins/Handler.hh>
#include <GazeboYarpPlugins/ConfHelpers.hh>

#include <gazebo/physics/Model.hh>
#include <yarp/dev/Wrapper.h>
#include <yarp/os/Network.h>

using namespace std;
namespace gazebo
{

GZ_REGISTER_MODEL_PLUGIN(GazeboYarpTripod)

    GazeboYarpTripod::GazeboYarpTripod() : m_iWrap(0)
    {}

    GazeboYarpTripod::~GazeboYarpTripod()
    {
        if (m_iWrap) {
            m_iWrap->detachAll();
            m_iWrap = 0;
        }
        if (m_wrapper.isValid())
            m_wrapper.close();

        for (int n = 0; n < m_controlBoards.size(); n++) {
            std::string scopedDeviceName = m_robotName + "::" + m_controlBoards[n]->key.c_str();
            GazeboYarpPlugins::Handler::getHandler()->removeDevice(scopedDeviceName);
        }

        GazeboYarpPlugins::Handler::getHandler()->removeRobot(m_robotName);
        yarp::os::Network::fini();
        std::cout<<"Goodbye!"<<std::endl;
    }

    /**
     * Saves the gazebo pointer, creates the device driver
     */
    void GazeboYarpTripod::Load(physics::ModelPtr _parent, sdf::ElementPtr _sdf)
    {
        yarp::os::Network::init();
        if (!yarp::os::Network::checkNetwork(GazeboYarpPlugins::yarpNetworkInitializationTimeout)) {
            std::cerr << "GazeboYarpTripod::Load error: yarp network does not seem to be available, is the yarpserver running?"<<std::endl;
            return;
        }
        std::cout<<"*** GazeboYarpTripod plugin started ***"<<std::endl;

        if (!_parent) {
            gzerr << "GazeboYarpTripod plugin requires a parent.\n";
            return;
        }

        m_robotName = _parent->GetScopedName();
        GazeboYarpPlugins::Handler::getHandler()->setRobot(get_pointer(_parent));

        // Add the gazebo_controlboard device driver to the factory.
        yarp::dev::Drivers::factory().add(new yarp::dev::DriverCreatorOf<cer::dev::GazeboTripodMotionControl>("gazebo_tripod", "controlboardwrapper2", "GazeboTripodMotionControl"));

        //Getting .ini configuration file from sdf
        bool configuration_loaded = false;

        yarp::os::Bottle wrapper_group;
        yarp::os::Bottle driver_group;
        if (_sdf->HasElement("yarpConfigurationFile")) {
            std::string ini_file_name = _sdf->Get<std::string>("yarpConfigurationFile");
            std::string ini_file_path = gazebo::common::SystemPaths::Instance()->FindFileURI(ini_file_name);

            GazeboYarpPlugins::addGazeboEnviromentalVariablesModel(_parent,_sdf,m_parameters);

            bool wipe = false;
            if (ini_file_path != "" && m_parameters.fromConfigFile(ini_file_path.c_str(),wipe))
            {
                std::cout << "GazeboYarpTripod: Found yarpConfigurationFile: loading from " << ini_file_path << std::endl;
                m_parameters.put("gazebo_ini_file_path",ini_file_path.c_str());

                wrapper_group = m_parameters.findGroup("WRAPPER");
                if(wrapper_group.isNull()) {
                    printf("GazeboYarpTripod::Load  Error: [WRAPPER] group not found in config file\n");
                    return;
                }
                if(m_parameters.check("ROS"))
                {
                    yarp::os::ConstString ROS;
                    ROS = yarp::os::ConstString ("(") + m_parameters.findGroup("ROS").toString() + yarp::os::ConstString (")");
                    wrapper_group.append(yarp::os::Bottle(ROS));
                }
                configuration_loaded = true;
            }

        }

        if (!configuration_loaded) {
            std::cout << "GazeboYarpTripod: File .ini not found, quitting\n" << std::endl;
            return;
        }

        m_wrapper.open(wrapper_group);

        if (!m_wrapper.isValid())
            fprintf(stderr, "GazeboYarpTripod: wrapper did not open\n");
        else
            fprintf(stderr, "GazeboYarpTripod: wrapper opened correctly\n");

        if (!m_wrapper.view(m_iWrap)) {
            printf("Wrapper interface not found\n");
        }

        yarp::os::Bottle *netList = wrapper_group.find("networks").asList();
        if (netList->isNull()) {
            printf("GazeboYarpTripod ERROR, net list to attach to was not found, exiting\n");
            m_wrapper.close();
            // m_controlBoard.close();
            return;
        }

        for (int n = 0; n < netList->size(); n++)
        {
            yarp::dev::PolyDriverDescriptor newPoly;

            newPoly.key = netList->get(n).asString();
            std::string scopedDeviceName = m_robotName + "::" + newPoly.key.c_str();
            newPoly.poly = GazeboYarpPlugins::Handler::getHandler()->getDevice(scopedDeviceName);

            if( newPoly.poly != NULL)
            {
                // device already exists, use it, setting it againg to increment the usage counter.
                printf("controlBoard %s already opened\n", newPoly.key.c_str());

            }
            else
            {
                driver_group = m_parameters.findGroup(newPoly.key.c_str());
                if (driver_group.isNull()) {
                    fprintf(stderr, "GazeboYarpTripod::Load  Error: [%s] group not found in config file. Closing wrapper \n", newPoly.key.c_str());
                    m_wrapper.close();
                    return;
                }

                m_parameters.put("name", newPoly.key.c_str());
                m_parameters.fromString(driver_group.toString(), false);
                m_parameters.put("robotScopedName", m_robotName);
                std::cout << "GazeboYarpTripod: setting robotScopedName " << m_robotName << std::endl;
                 //std::cout << "before open: params are " << m_parameters.toString() << std::endl;

                if (_sdf->HasElement("initialConfiguration")) {
                    std::string configuration_s = _sdf->Get<std::string>("initialConfiguration");
                    m_parameters.put("initialConfiguration", configuration_s.c_str());
                }

                 newPoly.poly = new yarp::dev::PolyDriver;
                if(! newPoly.poly->open(m_parameters) || ! newPoly.poly->isValid())
                {
                    std::cerr << "controlBoard <" << newPoly.key << "> did not open!!";
                    for(int idx=0; idx<m_controlBoards.size(); idx++)
                    {
                        m_controlBoards[idx]->poly->close();
                    }
                    m_wrapper.close();
                    return;
                }
                else
                {
                    printf("controlBoard %s opened correctly\n", newPoly.key.c_str());
                }
            }
            GazeboYarpPlugins::Handler::getHandler()->setDevice(scopedDeviceName, newPoly.poly);
            m_controlBoards.push(newPoly);
        }

        if (!m_iWrap || !m_iWrap->attachAll(m_controlBoards))
        {
            printf("GazeboYarpTripod: Error while attaching wrapper to device\n");
            m_wrapper.close();
            for (int n = 0; n < netList->size(); n++) {
                std::string scopedDeviceName = m_robotName + "::" + m_controlBoards[n]->key.c_str();
                GazeboYarpPlugins::Handler::getHandler()->removeDevice(scopedDeviceName);
            }
            return;
        }

        printf("Device initialized correctly, now sitting and waiting cause I am just the main of the yarp device, and the robot is linked to the onUpdate event of gazebo\n");
        std::cout<<"Loaded GazeboYarpTripod Plugin"<<std::endl;
    }

}
