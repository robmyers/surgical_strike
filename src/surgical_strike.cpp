/*
  Surgical Strike (Free Software Version).
  Copyright (C) 2008, 2014 Rob Myers

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define MANOUVER_X_RELATIVE (1)

////////////////////////////////////////////////////////////////////////////////
// Includes
////////////////////////////////////////////////////////////////////////////////

#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <unistd.h>
#include <fcntl.h>

#include <osg/Group>
#include <osg/LightModel>
#include <osg/Material>
#include <osg/MatrixTransform>
#include <osg/Node>
#include <osg/Texture2D>
#include <osg/TexGen>
#include <osg/Vec3f>

//#include <osg/ShapeDrawable>

#include <osgDB/Registry>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>

#include <osgViewer/Viewer>


////////////////////////////////////////////////////////////////////////////////
// Externs
////////////////////////////////////////////////////////////////////////////////

extern int yylineno;

//TODO - Make this settable from the command line
bool debug = true;


////////////////////////////////////////////////////////////////////////////////
// Constants
////////////////////////////////////////////////////////////////////////////////

const char * MAIN = "Main entry point";

const double PI = 3.14159265;
const double DEGS_TO_RADS = PI / 180.0;

////////////////////////////////////////////////////////////////////////////////
// Base class for commands
// Each codeword is a list of subclasses of this.
////////////////////////////////////////////////////////////////////////////////

struct Command
{
    Command () {}
    virtual ~Command () {}
    virtual void execute () = 0;
};


////////////////////////////////////////////////////////////////////////////////
// Globals
////////////////////////////////////////////////////////////////////////////////

// Cache for payload models
std::map <std::string, osg::Node *> payloads;

// Cache for payload model spherical bounds size, which we use as payload length
std::map <osg::Node *, double> payload_sizes;

// Cache for camouflage textures
std::map <std::string, osg::Texture2D *> camouflages;

// The current camouflage
osg::Texture2D * current_camouflage = NULL;

// The current payload
osg::Node * current_payload = NULL;

// The root node of the scene graph
osg::Group * theater = NULL;

// The transforms
std::vector<osg::Vec3d> origin_stack;
std::vector<osg::Vec3d> position_stack;
std::vector<osg::Vec3d> rotation_stack;
std::vector<osg::Vec3d> scale_stack;

// The codewords.
// We should use smart pointers rather than pointers because the STL sucks.
std::map <std::string, std::vector <Command*> > codewords;

// The name of the codeword that is currently being parsed, or MAIN.
std::string current_codeword = MAIN;


////////////////////////////////////////////////////////////////////////////////
// Functions
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
// Geometry Utility
////////////////////////////////////////////////////////////////////////////////


osg::Vec3d spherical_to_cartesian_radians (osg::Vec3d & spher, double scale)
{
    double r = spher.x ();
    double lambda = spher.y ();
    double phi = spher.z ();
    
    double sphi = sin (phi);
    double cphi = cos (phi);
    double slambda = sin (lambda);
    double clambda = cos (lambda);
    double x = scale * r * cphi * clambda;
    double y = scale * r * cphi * slambda;
    double z = scale * r * sphi;

    return osg::Vec3d(x, y, z);
}

osg::Vec3d spherical_to_cartesian (osg::Vec3d & spher, double scale)
{
    osg::Vec3d rads(spher.x (),
                    spher.y () * DEGS_TO_RADS,
                    spher.z () * DEGS_TO_RADS);
    return spherical_to_cartesian_radians(rads, scale);
}

////////////////////////////////////////////////////////////////////////////////
// Current Transformation Matrix management
////////////////////////////////////////////////////////////////////////////////

osg::Vec3d & origin ()
{
    return origin_stack.back ();
}

osg::Vec3d & position ()
{
    return position_stack.back ();
}

osg::Vec3d cartesian_position ()
{
    double size = payload_sizes[current_payload];
    return spherical_to_cartesian (position (), size);
}

osg::Vec3d & rotation ()
{
    return rotation_stack.back ();
}

osg::Vec3d & scale ()
{
    return scale_stack.back ();
}

osg::Matrixd origin_transform ()
{
    osg::Matrixd matrix;
    matrix.makeTranslate (origin ());
    return matrix;
}

osg::Matrixd origin_transform_from ()
{
    osg::Matrixd matrix;
    matrix.makeTranslate (origin () + cartesian_position ());
    return matrix;
}

osg::Matrixd origin_transform_to ()
{
    osg::Matrixd matrix;
    matrix.makeTranslate (- (origin () + cartesian_position ()));
    return matrix;
}

osg::Matrixd position_transform ()
{
    osg::Matrixd matrix;
    matrix.makeTranslate (cartesian_position ());
    return matrix;
}

osg::Matrixd rotation_transform ()
{
    osg::Vec3f rotate = rotation ();
    osg::Matrixd matrix = origin_transform_to ();
    if (rotate.x () != 0.0)
    {
        osg::Matrixd rotatex;
        rotatex.makeRotate (rotate.x () * DEGS_TO_RADS, 1.0, 0, 0);
        matrix *= rotatex;
    }
    if (rotate.y () != 0.0)
    {
        osg::Matrixd rotatey;
        rotatey.makeRotate (rotate.y () * DEGS_TO_RADS, 0.0, 1.0, 0);
        matrix *= rotatey;
    }
    if (rotate.z () != 0.0)
    {
        osg::Matrixd rotatez;
        rotatez.makeRotate (rotate.z () * DEGS_TO_RADS, 0.0, 0.0, 1.0);
        matrix  *= rotatez;
    }

    matrix *= origin_transform_from ();

    return matrix;
}

osg::Matrixd scale_transform ()
{
    osg::Matrixd matrix;// = origin_transform_to ();

    osg::Matrixd scaling;
    scaling.makeScale (scale ());
    matrix *= scaling;
    
    //matrix *= origin_transform_from ();
    return matrix;
}

osg::Matrixd current_transform ()
{
    return origin_transform () *
        position_transform () *
        scale_transform () *
        rotation_transform () ;
}

void push_transforms ()
{
    osg::Vec3d zero (0.0, 0.0, 0.0);
    // FIXME: If you mark after loading a new model of a different size
    // this will set the origin based on the new size.
    // For the moment just make sure to load inside marks.
    origin_stack.push_back (cartesian_position ());
    position_stack.push_back (zero);
    rotation_stack.push_back (rotation ());
    scale_stack.push_back (scale ());
}

void pop_transforms ()
{
    origin_stack.pop_back ();
    position_stack.pop_back ();
    rotation_stack.pop_back ();
    scale_stack.pop_back ();

    //TODO: Issue helpful warning message
    assert (! origin_stack.empty ());
}

void initialize_transforms ()
{
    osg::Vec3d zero (0.0, 0.0, 0.0);
    origin_stack.push_back (zero);
    position_stack.push_back (zero);
    rotation_stack.push_back (zero);
    osg::Vec3d one (1.0, 1.0, 1.0);
    scale_stack.push_back (one);
}


////////////////////////////////////////////////////////////////////////////////
// Commands
////////////////////////////////////////////////////////////////////////////////

struct Incoming : public Command
{
    virtual void execute ()
    {
        assert (theater == NULL);
        assert (current_camouflage == NULL);
        assert (current_payload == NULL);
        assert (origin_stack.empty ());

        if (debug)
            std::fprintf (stderr, "Executing incoming!\n");

        theater = new osg::Group;
        initialize_transforms ();

        current_codeword = MAIN;
    }
};

struct Manouver : public Command
{
    // For historical reasons, x is absolute but y and z are relative
    // unless we enable otherwise
    float x, y, z;

    Manouver (float n1, float n2, float n3)
    {
        x = n1;
        y = n2;
        z = n3;
    }

    virtual void execute ()
    {
        if (debug)
            std::fprintf (stderr, "Executing manouver %f %f %f\n", x, y, z);

        osg::Vec3d & spherical = position ();
        spherical.x () =
#ifdef MANOUVER_X_RELATIVE
            spherical.x () +
#endif
            x;
        spherical.y () = spherical.y () + y;
        spherical.z () = spherical.z () + z;
        
        position () = spherical;
    }
};

struct Roll : public Command
{
    double x, y, z;

    Roll (float n1, float n2, float n3)
    {
        x += n1;
        y += n3;
        z += n2;
    }

    virtual void execute ()
    {
        if (debug)
            std::fprintf (stderr, "Executing roll %f %f %f\n", x, y, z);
        rotation ().x () += x;
        rotation ().y () += y;
        rotation ().z () += z;
    }
};

struct Scale : public Command
{
    float x, y, z;

    Scale (float n1, float n2, float n3)
    {
        x = n1;
        y = n2;
        z = n3;
    }

    virtual void execute ()
    {
        if (debug)
            std::fprintf (stderr, "Executing scale %f %f %f\n", x, y, z);
        scale ().x () += x;
        scale ().y () += y;
        scale ().z () += z;
    }
};

struct Mark : public Command
{
    virtual void execute ()
    {
        assert (theater != NULL);
        if (debug)
            std::fprintf (stderr, "Executing mark\n");
        push_transforms ();
    }
};

struct Clear : public Command
{
    virtual void execute ()
    {
        assert (theater != NULL);
        if (debug)
            std::fprintf (stderr, "Executing clear\n");
        pop_transforms ();
    }
};

bool file_exists (const std::string filename)
{
    bool exists = false;
    int fd = open (filename.c_str (), O_RDONLY);
    if (fd != -1)
    {
        close (fd);
        exists = true;
    }
    return exists;
}

struct Camouflage : public Command
{
    std::string camouflage_file_name;

    Camouflage (std::string & filename)
    {
        camouflage_file_name = filename;
    }

    osg::Texture2D * imageToTexture (osg::Image * image)
    {
        assert (image != NULL);
        osg::Texture2D * texture = new osg::Texture2D;
        assert (texture != NULL);
        texture->setDataVariance (osg::Object::DYNAMIC);
        texture->setFilter (osg::Texture::MIN_FILTER, osg::Texture::LINEAR_MIPMAP_LINEAR);
        texture->setFilter (osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
        texture->setWrap (osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        texture->setWrap (osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
        texture->setImage (image);
        return texture;
    }

    virtual void execute ()
    {
        assert (theater != NULL);
        assert (camouflage_file_name != "");
        if (debug)
            std::fprintf (stderr, "Loading camouflage: %s ",
                          camouflage_file_name.c_str ());
        if (camouflages.find (camouflage_file_name) != camouflages.end ())
        {
            if (debug)
                std::fprintf (stderr, "from cache.\n");
            current_camouflage = camouflages [camouflage_file_name];
        }
        else
        {
            if (debug)
                std::fprintf (stderr, "from file.\n");
            if (! file_exists (camouflage_file_name))
            {
                std::fprintf (stderr, "Cannot find camouflage %s. Line %i.\n",
                              camouflage_file_name.c_str (), yylineno);
                exit (1);
            }

            osg::Image * image_from_file =
                osgDB::readImageFile (camouflage_file_name);
            osg::Texture2D * camouflage_from_file =
                imageToTexture(image_from_file);
            camouflages [camouflage_file_name] = camouflage_from_file;
            current_camouflage = camouflage_from_file;
            if (debug)
                std::fprintf (stderr, "Loaded.\n");
        }
    }
};

struct Payload : public Command
{
    std::string payload_file_name;

    Payload (std::string & filename)
    {
        payload_file_name = filename;
    }

    osg::Node * scaleToUnit (osg::Node * payload)
    {
        const osg::BoundingSphere & bounds = payload->getBound();
        float diameter = bounds.radius() * 2.0;
        float factor = 1.0 / diameter;
        osg::Matrix matrix;
        matrix.makeScale(factor, factor, factor);
        osg::MatrixTransform * scaled_node = new osg::MatrixTransform();
        scaled_node->ref();
        scaled_node->setMatrix(matrix);
        scaled_node->addChild(payload);
        return scaled_node;
    }

    virtual void execute ()
    {
        assert (theater != NULL);
        assert (payload_file_name != "");
        if (debug)
            std::fprintf (stderr, "Loading payload: %s ",
                          payload_file_name.c_str ());
        if (payloads.find (payload_file_name) != payloads.end ())
        {
            if (debug)
                std::fprintf (stderr, "from cache.\n");
            current_payload = payloads [payload_file_name];
        }
        else
        {
            if (debug)
                std::fprintf (stderr, "from file.\n");
            if (! file_exists (payload_file_name))
            {
                std::fprintf (stderr, "Couldn't find payload %s\n",
                              payload_file_name.c_str ());
                exit (1);
            }
            osg::Node * payload_read = osgDB::readNodeFile (payload_file_name);
            if (payload_read == NULL)
            {
                std::fprintf (stderr, "Couldn't load file %s\n",
                              payload_file_name.c_str ());
                exit (1);

            }
            payloads [payload_file_name] = payload_read;
            payload_sizes [payload_read] = payload_read->getBound().radius();
            current_payload = payload_read;
            if (debug)
                std::fprintf (stderr, "Loaded.\n");
        }
    }
};

struct Deliver : public Command
{
    virtual void execute ()
    {
        assert (theater != NULL);
        if (current_payload == NULL)
        {
            std::fprintf (stderr, "Cannot deliver, no payload.\n");
            exit (1);
        }
        if (debug)
            std::fprintf (stderr, "Delivering payload\n");

        osg::Node * deliver;
        deliver = (osg::Node*)current_payload->clone (osg::CopyOp::SHALLOW_COPY);

        if (current_camouflage != NULL)
        {
            //if (debug)
            //    std::fprintf (stderr, "Camouflaging.\n");
            osg::ref_ptr<osg::StateSet> stateset = deliver->getOrCreateStateSet ();
            assert (stateset != NULL);
            osg::ref_ptr<osg::LightModel> lightModel = new osg::LightModel;
            lightModel->setTwoSided(true);
            stateset->setAttributeAndModes(lightModel.get());
            
            stateset->setTextureAttributeAndModes (0, current_camouflage,
                                                   osg::StateAttribute::ON
                                                   | osg::StateAttribute::OVERRIDE);
            
            osg::ref_ptr<osg::TexGen> texGen(new osg::TexGen());
            const osg::BoundingSphere & bounds = deliver->getBound();
            float factor = 1.0 / bounds.radius();
            texGen->setPlane(osg::TexGen::S, osg::Plane(factor, 0.0, 0.0, 0.5));
            texGen->setPlane(osg::TexGen::T, osg::Plane(0.0, factor, 0.0, 0.5));
            stateset->setTextureAttributeAndModes(0, texGen);
        }
        osg::MatrixTransform * target =
            new osg::MatrixTransform (current_transform ());

        target->addChild (deliver);
        theater->addChild (target);
    }
};

struct CodewordExecution : public Command
{
    std::string codeword;
    int times;

    CodewordExecution (const char * word, int count)
    {
        codeword = word;
        times = count;
    }

    virtual void execute ()
    {
        // incoming! will create theater, and it's called from MAIN
        //assert (theater != NULL);
        if (codewords.find (codeword) == codewords.end ())
        {
            std::fprintf (stderr, "Cannot execute codeword: %s, "
                          "no such codeword at line %i.\n",
                          codeword.c_str (), yylineno);
            exit (1);
        }
        if (debug)
            std::fprintf (stderr, "Executing: %s %i time(s)\n",
                          codeword.c_str (), times);
        /*SoSeparator * codeword_separator = new SoSeparator;
          codeword_separator->ref ();
          codewords [word] = codeword;
          push_target (codeword); */

        std::vector<Command *> commands = codewords[codeword];
        for (int i = 0; i < times; i++)
        {
            for (size_t j = 0; j < commands.size (); j++)
            {
                commands[j]->execute ();
            }
        }

        //pop_target ();
    }
};


////////////////////////////////////////////////////////////////////////////////
// Parsing
////////////////////////////////////////////////////////////////////////////////

void add_command_to_current_codeword (Command * to_add)
{
    assert (to_add != NULL);
    codewords[current_codeword].push_back (to_add);
}

void parse_incoming ()
{
    if (debug)
        std::fprintf (stderr, "Parsing incoming!\n");
    add_command_to_current_codeword (new Incoming ());
}

void parse_manouver (float x, float y, float z)
{
    if (debug)
        std::fprintf (stderr, "Parsing manouver %f %f %f\n", x, y, z);
    add_command_to_current_codeword (new Manouver (x, y, z));
}

void parse_roll (float x, float y, float z)
{
    if (debug)
        std::fprintf (stderr, "Parsing roll %f %f %f\n", x, y, z);
    add_command_to_current_codeword (new Roll (x, y, z));
}

void parse_scale (float x, float y, float z)
{
    if (debug)
        std::fprintf (stderr, "Parsing scale %f %f %f\n", x, y, z);
    add_command_to_current_codeword (new Scale (x, y, z));
}

void parse_codeword (std::string word)
{
    assert (current_codeword == MAIN);
    assert (word != "");
    if (debug)
        std::fprintf (stderr, "Parsing codeword %s\n", word.c_str ());
    current_codeword = word;
}

void parse_set ()
{
    assert (current_codeword != MAIN);
    if (debug)
        std::fprintf (stderr, "Parsing set\n");
    current_codeword = MAIN;
}

void parse_mark ()
{
    if (debug)
        std::fprintf (stderr, "Parsing mark\n");
    add_command_to_current_codeword (new Mark ());
}

void parse_clear ()
{
    if (debug)
        std::fprintf (stderr, "Parsing clear\n");
    add_command_to_current_codeword (new Clear ());
}

void parse_camouflage (std::string camouflage_file_name)
{
    if (debug)
        std::fprintf (stderr, "Parsing camouflage %s\n",
                      camouflage_file_name.c_str ());
    add_command_to_current_codeword (new Camouflage (camouflage_file_name));
}

void parse_payload (std::string payload_file_name)
{
    if (debug)
        std::fprintf (stderr, "Parsing payload %s\n", payload_file_name.c_str ());
    add_command_to_current_codeword (new Payload (payload_file_name));
}

void parse_deliver ()
{
    if (debug)
        std::fprintf (stderr, "Parsing deliver\n");
    add_command_to_current_codeword (new Deliver ());
}

void parse_codeword_execution (const char * codeword, int times)
{
    if (debug)
        std::fprintf (stderr, "Parsing codeword execution %s %i\n",
                      codeword, times);
    add_command_to_current_codeword (new CodewordExecution (codeword, times));
}


////////////////////////////////////////////////////////////////////////////////
// Main program lifecycle
////////////////////////////////////////////////////////////////////////////////

void write_file (const std::string & filename)
{
    std::fprintf (stderr, "Writing file %s\n", filename.c_str ());
    bool ok = osgDB::writeNodeFile (*theater, filename);
    if (! ok)
    {
        std::fprintf (stderr, "Couldn't write file %s\n", filename.c_str ());
        exit (1);
    }
}

void run_main (const std::string & savefilename)
{
    CodewordExecution main (MAIN, 1);
    main.execute ();

    if (debug) std::fprintf (stderr, "Writing output file.\n");
    write_file (savefilename);
    if (debug) std::fprintf (stderr, "Finished.\n");

    osgViewer::Viewer viewer;
    viewer.setSceneData (theater);
    viewer.realize ();
    viewer.run ();
}
