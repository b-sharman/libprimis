/* rendermodel.cpp: world static and dynamic models
 *
 * Libprimis can handle static ("mapmodel") type models which are placed in levels
 * as well as dynamic, animated models such as players or other actors. For animated
 * models, the md5 model format is supported; simpler static models can use the
 * common Wavefront (obj) model format.
 *
 */
#include "../libprimis-headers/cube.h"
#include "../../shared/geomexts.h"
#include "../../shared/glemu.h"
#include "../../shared/glexts.h"
#include "../../shared/stream.h"

#include "aa.h"
#include "csm.h"
#include "radiancehints.h"
#include "rendergl.h"
#include "renderlights.h"
#include "rendermodel.h"
#include "renderva.h"
#include "renderwindow.h"
#include "shaderparam.h"
#include "texture.h"

#include "interface/console.h"
#include "interface/control.h"
#include "interface/cs.h"

#include "world/entities.h"
#include "world/octaedit.h"
#include "world/octaworld.h"
#include "world/physics.h"
#include "world/bih.h"
#include "world/world.h"

VAR(oqdynent, 0, 1, 1); //occlusion query dynamic ents

int numanims; //set by game at runtime
std::vector<std::string> animnames; //set by game at runtime

model *loadingmodel = nullptr;

//need the above vars inited before these headers will load properly

#include "model/model.h"
#include "model/ragdoll.h"
#include "model/animmodel.h"
#include "model/vertmodel.h"
#include "model/skelmodel.h"

#include "model/hitzone.h"

model *loadmapmodel(int n)
{
    if(static_cast<int>(mapmodels.size()) > n)
    {
        model *m = mapmodels[n].m;
        return m ? m : loadmodel(nullptr, n);
    }
    return nullptr;
}

//need the above macros & fxns inited before these headers will load properly
#include "model/md5.h"
#include "model/obj.h"

/* note about objcommands variable:
 *
 * this variable is never used anywhere at all in the codebase
 * it only exists to call its constructor which adds commands to the cubescript
 * ident hash table of the given template type (obj)
 */
static vertcommands<obj> objcommands;

static void checkmdl()
{
    if(!loadingmodel)
    {
        conoutf(Console_Error, "not loading a model");
        return;
    }
}

static void mdlcullface(int *cullface)
{
    checkmdl();
    loadingmodel->setcullface(*cullface);
}

static void mdlcolor(float *r, float *g, float *b)
{
    checkmdl();
    loadingmodel->setcolor(vec(*r, *g, *b));
}

static void mdlcollide(int *collide)
{
    checkmdl();
    loadingmodel->collide = *collide!=0 ? (loadingmodel->collide ? loadingmodel->collide : Collide_OrientedBoundingBox) : Collide_None;
}

static void mdlellipsecollide(int *collide)
{
    checkmdl();
    loadingmodel->collide = *collide!=0 ? Collide_Ellipse : Collide_None;
}

static void mdltricollide(char *collide)
{
    checkmdl();
    delete[] loadingmodel->collidemodel;
    loadingmodel->collidemodel = nullptr;
    char *end = nullptr;
    int val = strtol(collide, &end, 0);
    if(*end)
    {
        val = 1;
        loadingmodel->collidemodel = newstring(collide);
    }
    loadingmodel->collide = val ? Collide_TRI : Collide_None;
}

static void mdlspec(float *percent)
{
    checkmdl();
    float spec = *percent > 0 ? *percent/100.0f : 0.0f;
    loadingmodel->setspec(spec);
}

static void mdlgloss(int *gloss)
{
    checkmdl();
    loadingmodel->setgloss(std::clamp(*gloss, 0, 2));
}

static void mdlalphatest(float *cutoff)
{
    checkmdl();
    loadingmodel->setalphatest(std::max(0.0f, std::min(1.0f, *cutoff)));
}

static void mdldepthoffset(int *offset)
{
    checkmdl();
    loadingmodel->depthoffset = *offset!=0;
}

static void mdlglow(float *percent, float *delta, float *pulse)
{
    checkmdl();
    float glow = *percent > 0 ? *percent/100.0f : 0.0f,
          glowdelta = *delta/100.0f,
          glowpulse = *pulse > 0 ? *pulse/1000.0f : 0;
    glowdelta -= glow;
    loadingmodel->setglow(glow, glowdelta, glowpulse);
}

static void mdlfullbright(float *fullbright)
{
    checkmdl();
    loadingmodel->setfullbright(*fullbright);
}


static void mdlshader(char *shader)
{
    checkmdl();
    loadingmodel->setshader(lookupshaderbyname(shader));
}


//assigns a new spin speed in three euler angles for the model object currently being loaded
static void mdlspin(float *yaw, float *pitch, float *roll)
{
    checkmdl();
    loadingmodel->spinyaw = *yaw;
    loadingmodel->spinpitch = *pitch;
    loadingmodel->spinroll = *roll;
}

//assigns a new scale factor in % for the model object currently being loaded
static void mdlscale(float *percent)
{
    checkmdl();
    float scale = *percent > 0 ? *percent/100.0f : 1.0f;
    loadingmodel->scale = scale;
}

//assigns translation in x,y,z in cube units for the model object currently being loaded
static void mdltrans(float *x, float *y, float *z)
{
    checkmdl();
    loadingmodel->translate = vec(*x, *y, *z);
}

//assigns angle to the offsetyaw field of the model object currently being loaded
static void mdlyaw(float *angle)
{
    checkmdl();
    loadingmodel->offsetyaw = *angle;
}


//assigns angle to the offsetpitch field of the model object currently being loaded
static void mdlpitch(float *angle)
{
    checkmdl();
    loadingmodel->offsetpitch = *angle;
}

//assigns angle to the offsetroll field of the model object currently being loaded
static void mdlroll(float *angle)
{
    checkmdl();
    loadingmodel->offsetroll = *angle;
}

//assigns shadow to the shadow field of the model object currently being loaded
static void mdlshadow(int *shadow)
{
    checkmdl();
    loadingmodel->shadow = *shadow!=0;
}

//assigns alphashadow to the alphashadow field of the model object currently being loaded
static void mdlalphashadow(int *alphashadow)
{
    checkmdl();
    loadingmodel->alphashadow = *alphashadow!=0;
}

//assigns rad, h, eyeheight to the fields of the model object currently being loaded
static void mdlbb(float *rad, float *h, float *eyeheight)
{
    checkmdl();
    loadingmodel->collidexyradius = *rad;
    loadingmodel->collideheight = *h;
    loadingmodel->eyeheight = *eyeheight;
}

static void mdlextendbb(float *x, float *y, float *z)
{
    checkmdl();
    loadingmodel->bbextend = vec(*x, *y, *z);
}

/* mdlname
 *
 * returns the name of the model currently loaded [most recently]
 */
static void mdlname()
{
    checkmdl();
    result(loadingmodel->name);
}

//========================================================= CHECK_RAGDOLL
#define CHECK_RAGDOLL \
    checkmdl(); \
    if(!loadingmodel->skeletal()) \
    { \
        conoutf(Console_Error, "not loading a skeletal model"); \
        return; \
    } \
    skelmodel *m = static_cast<skelmodel *>(loadingmodel); \
    if(m->parts.empty()) \
    { \
        return; \
    } \
    skelmodel::skelmeshgroup *meshes = static_cast<skelmodel::skelmeshgroup *>(m->parts.last()->meshes); \
    if(!meshes) \
    { \
        return; \
    } \
    skelmodel::skeleton *skel = meshes->skel; \
    if(!skel->ragdoll) \
    { \
        skel->ragdoll = new ragdollskel; \
    } \
    ragdollskel *ragdoll = skel->ragdoll; \
    if(ragdoll->loaded) \
    { \
        return; \
    }

static void rdvert(float *x, float *y, float *z, float *radius)
{
    CHECK_RAGDOLL;
    ragdollskel::vert v;
    v.pos = vec(*x, *y, *z);
    v.radius = *radius > 0 ? *radius : 1;
    ragdoll->verts.push_back(v);
}

/* ragdoll eye level: sets the ragdoll's eye point to the level passed
 * implicitly modifies the ragdoll selected by CHECK_RAGDOLL
 */
static void rdeye(int *v)
{
    CHECK_RAGDOLL;
    ragdoll->eye = *v;
}

static void rdtri(int *v1, int *v2, int *v3)
{
    CHECK_RAGDOLL;
    ragdollskel::tri t;
    t.vert[0] = *v1;
    t.vert[1] = *v2;
    t.vert[2] = *v3;
    ragdoll->tris.emplace_back(t);
}

static void rdjoint(int *n, int *t, int *v1, int *v2, int *v3)
{
    CHECK_RAGDOLL;
    if(*n < 0 || *n >= skel->numbones)
    {
        return;
    }
    ragdollskel::joint j;
    j.bone = *n;
    j.tri = *t;
    j.vert[0] = *v1;
    j.vert[1] = *v2;
    j.vert[2] = *v3;
    ragdoll->joints.push_back(j);
}

static void rdlimitdist(int *v1, int *v2, float *mindist, float *maxdist)
{
    CHECK_RAGDOLL;
    ragdollskel::distlimit d;
    d.vert[0] = *v1;
    d.vert[1] = *v2;
    d.mindist = *mindist;
    d.maxdist = std::max(*maxdist, *mindist);
    ragdoll->distlimits.push_back(d);
}

static void rdlimitrot(int *t1, int *t2, float *maxangle, float *qx, float *qy, float *qz, float *qw)
{
    CHECK_RAGDOLL;
    ragdollskel::rotlimit r;
    r.tri[0] = *t1;
    r.tri[1] = *t2;
    r.maxangle = *maxangle / RAD;
    r.maxtrace = 1 + 2*std::cos(r.maxangle);
    r.middle = matrix3(quat(*qx, *qy, *qz, *qw));
    ragdoll->rotlimits.push_back(r);
}

static void rdanimjoints(int *on)
{
    CHECK_RAGDOLL;
    ragdoll->animjoints = *on!=0;
}

#undef CHECK_RAGDOLL
//==============================================================================
// mapmodels

std::vector<mapmodelinfo> mapmodels;
static const char * const mmprefix = "mapmodel/";
static const int mmprefixlen = std::strlen(mmprefix);

void mapmodel(char *name)
{
    mapmodelinfo mmi;
    if(name[0])
    {
        formatstring(mmi.name, "%s%s", mmprefix, name);
    }
    else
    {
        mmi.name[0] = '\0';
    }
    mmi.m = mmi.collide = nullptr;
    mapmodels.push_back(mmi);
}

void mapmodelreset(int *n)
{
    if(!(identflags&Idf_Overridden) && !allowediting)
    {
        return;
    }
    mapmodels.resize(std::clamp(*n, 0, static_cast<int>(mapmodels.size())));
}

const char *mapmodelname(int i)
{
    return (static_cast<int>(mapmodels.size()) > i) ? mapmodels[i].name : nullptr;
}

void mapmodelnamecmd(int *index, int *prefix)
{
    if(static_cast<int>(mapmodels.size()) > *index)
    {
        result(mapmodels[*index].name[0] ? mapmodels[*index].name + (*prefix ? 0 : mmprefixlen) : "");
    }
}

void mapmodelloaded(int *index)
{
    intret(static_cast<int>(mapmodels.size()) > *index && mapmodels[*index].m ? 1 : 0);
}

void nummapmodels()
{
    intret(mapmodels.size());
}

// model registry

hashnameset<model *> models;
vector<const char *> preloadmodels;
hashset<char *> failedmodels;

void preloadmodel(const char *name)
{
    if(!name || !name[0] || models.access(name) || preloadmodels.htfind(name) >= 0)
    {
        return;
    }
    preloadmodels.add(newstring(name));
}

void flushpreloadedmodels(bool msg)
{
    for(int i = 0; i < preloadmodels.length(); i++)
    {
        loadprogress = static_cast<float>(i+1)/preloadmodels.length();
        model *m = loadmodel(preloadmodels[i], -1, msg);
        if(!m)
        {
            if(msg)
            {
                conoutf(Console_Warn, "could not load model: %s", preloadmodels[i]);
            }
        }
        else
        {
            m->preloadmeshes();
            m->preloadshaders();
        }
    }
    preloadmodels.deletearrays();
    loadprogress = 0;
}

void preloadusedmapmodels(bool msg, bool bih)
{
    vector<extentity *> &ents = entities::getents();
    std::vector<int> used;
    for(int i = 0; i < ents.length(); i++)
    {
        extentity &e = *ents[i];
        if(e.type==EngineEnt_Mapmodel && e.attr1 >= 0 && std::find(used.begin(), used.end(), e.attr1) != used.end() )
        {
            used.push_back(e.attr1);
        }
    }

    vector<const char *> col;
    for(uint i = 0; i < used.size(); i++)
    {
        loadprogress = static_cast<float>(i+1)/used.size();
        int mmindex = used[i];
        if(!(static_cast<int>(mapmodels.size()) > (mmindex)))
        {
            if(msg)
            {
                conoutf(Console_Warn, "could not find map model: %d", mmindex);
            }
            continue;
        }
        mapmodelinfo &mmi = mapmodels[mmindex];
        if(!mmi.name[0])
        {
            continue;
        }
        model *m = loadmodel(nullptr, mmindex, msg);
        if(!m)
        {
            if(msg)
            {
                conoutf(Console_Warn, "could not load map model: %s", mmi.name);
            }
        }
        else
        {
            if(bih)
            {
                m->preloadBIH();
            }
            else if(m->collide == Collide_TRI && !m->collidemodel && m->bih)
            {
                m->setBIH();
            }
            m->preloadmeshes();
            m->preloadshaders();
            if(m->collidemodel && col.htfind(m->collidemodel) < 0)
            {
                col.add(m->collidemodel);
            }
        }
    }

    for(int i = 0; i < col.length(); i++)
    {
        loadprogress = static_cast<float>(i+1)/col.length();
        model *m = loadmodel(col[i], -1, msg);
        if(!m)
        {
            if(msg)
            {
                conoutf(Console_Warn, "could not load collide model: %s", col[i]);
            }
        }
        else if(!m->bih)
        {
            m->setBIH();
        }
    }

    loadprogress = 0;
}

model *loadmodel(const char *name, int i, bool msg)
{

    model *(__cdecl *md5loader)(const char *) = +[] (const char *filename) -> model* { return new md5(filename); };
    model *(__cdecl *objloader)(const char *) = +[] (const char *filename) -> model* { return new obj(filename); };

    std::vector<model *(__cdecl *)(const char *)> loaders;
    loaders.push_back(md5loader);
    loaders.push_back(objloader);

    if(!name)
    {
        if(!(static_cast<int>(mapmodels.size()) > i))
        {
            return nullptr;
        }
        mapmodelinfo &mmi = mapmodels[i];
        if(mmi.m)
        {
            return mmi.m;
        }
        name = mmi.name;
    }
    model **mm = models.access(name);
    model *m;
    if(mm)
    {
        m = *mm;
    }
    else
    {
        if(!name[0] || loadingmodel || failedmodels.find(name, nullptr))
        {
            return nullptr;
        }
        if(msg)
        {
            DEF_FORMAT_STRING(filename, "media/model/%s", name);
            renderprogress(loadprogress, filename);
        }
        for(model *(__cdecl *i)(const char *) : loaders)
        {
            m = i(name);
            if(!m)
            {
                continue;
            }
            loadingmodel = m;
            if(m->load())
            {
                break;
            }
            if(m)
            {
                delete m;
                m = nullptr;
            }
        }
        loadingmodel = nullptr;
        if(!m)
        {
            failedmodels.add(newstring(name));
            return nullptr;
        }
        models.access(m->name, m);
    }
    if((mapmodels.size() > static_cast<uint>(i)) && !mapmodels[i].m)
    {
        mapmodels[i].m = m;
    }
    return m;
}

void clear_models()
{
    ENUMERATE(models, model *, m, delete m);
}

void cleanupmodels()
{
    ENUMERATE(models, model *, m, m->cleanup());
}

static void clearmodel(char *name)
{
    model *m = models.find(name, nullptr);
    if(!m)
    {
        conoutf("model %s is not loaded", name);
        return;
    }
    for(uint i = 0; i < mapmodels.size(); i++)
    {
        mapmodelinfo &mmi = mapmodels[i];
        if(mmi.m == m)
        {
            mmi.m = nullptr;
        }
        if(mmi.collide == m)
        {
            mmi.collide = nullptr;
        }
    }
    models.remove(name);
    m->cleanup();
    delete m;
    conoutf("cleared model %s", name);
}

static bool modeloccluded(const vec &center, float radius)
{
    ivec bbmin(vec(center).sub(radius)),
         bbmax(vec(center).add(radius+1));
    return rootworld.bboccluded(bbmin, bbmax);
}

struct batchedmodel
{
    vec pos, center;
    float radius, yaw, pitch, roll, sizescale;
    vec4<float> colorscale;
    int anim, basetime, basetime2, flags, attached;
    union
    {
        int visible;
        int culled;
    };
    dynent *d;
    int next;
};
struct modelbatch
{
    model *m;
    int flags, batched;
};
static std::vector<batchedmodel> batchedmodels;
static std::vector<modelbatch> batches;
static std::vector<modelattach> modelattached;

void resetmodelbatches()
{
    batchedmodels.clear();
    batches.clear();
    modelattached.clear();
}

void addbatchedmodel(model *m, batchedmodel &bm, int idx)
{
    modelbatch *b = nullptr;
    if(batches.size() > m->batch)
    {
        b = &batches[m->batch];
        if(b->m == m && (b->flags & Model_Mapmodel) == (bm.flags & Model_Mapmodel))
        {
            goto foundbatch; //skip some shit
        }
    }
    m->batch = batches.size();
    batches.emplace_back();
    b = &batches.back();
    b->m = m;
    b->flags = 0;
    b->batched = -1;

foundbatch:
    b->flags |= bm.flags;
    bm.next = b->batched;
    b->batched = idx;
}

static void renderbatchedmodel(model *m, const batchedmodel &b)
{
    modelattach *a = nullptr;
    if(b.attached>=0)
    {
        a = &modelattached[b.attached];
    }
    int anim = b.anim;
    if(shadowmapping > ShadowMap_Reflect)
    {
        anim |= Anim_NoSkin;
    }
    else
    {
        if(b.flags&Model_FullBright)
        {
            anim |= Anim_FullBright;
        }
    }

    m->render(anim, b.basetime, b.basetime2, b.pos, b.yaw, b.pitch, b.roll, b.d, a, b.sizescale, b.colorscale);
}

//ratio between model size and distance at which to cull: at 200, model must be 200 times smaller than distance to model
VAR(maxmodelradiusdistance, 10, 200, 1000);

static void enablecullmodelquery()
{
    startbb();
}

static void rendercullmodelquery(model *m, dynent *d, const vec &center, float radius)
{
    if(std::fabs(camera1->o.x-center.x) < radius+1 &&
       std::fabs(camera1->o.y-center.y) < radius+1 &&
       std::fabs(camera1->o.z-center.z) < radius+1)
    {
        d->query = nullptr;
        return;
    }
    d->query = newquery(d);
    if(!d->query)
    {
        return;
    }
    d->query->startquery();
    int br = static_cast<int>(radius*2)+1;
    drawbb(ivec(static_cast<float>(center.x-radius), static_cast<float>(center.y-radius), static_cast<float>(center.z-radius)), ivec(br, br, br));
    endquery();
}

static int cullmodel(model *m, const vec &center, float radius, int flags, dynent *d = nullptr)
{
    if(flags&Model_CullDist && (center.dist(camera1->o) / radius) > maxmodelradiusdistance)
    {
        return Model_CullDist;
    }
    if(flags&Model_CullVFC && view.isfoggedsphere(radius, center))
    {
        return Model_CullVFC;
    }
    if(flags&Model_CullOccluded && modeloccluded(center, radius))
    {
        return Model_CullOccluded;
    }
    else if(flags&Model_CullQuery && d->query && d->query->owner==d && checkquery(d->query))
    {
        return Model_CullQuery;
    }
    return 0;
}

static int shadowmaskmodel(const vec &center, float radius)
{
    switch(shadowmapping)
    {
        case ShadowMap_Reflect:
            return calcspherersmsplits(center, radius);
        case ShadowMap_CubeMap:
        {
            vec scenter = vec(center).sub(shadoworigin);
            float sradius = radius + shadowradius;
            if(scenter.squaredlen() >= sradius*sradius)
            {
                return 0;
            }
            return calcspheresidemask(scenter, radius, shadowbias);
        }
        case ShadowMap_Cascade:
        {
            return csm.calcspherecsmsplits(center, radius);
        }
        case ShadowMap_Spot:
        {
            vec scenter = vec(center).sub(shadoworigin);
            float sradius = radius + shadowradius;
            return scenter.squaredlen() < sradius*sradius && sphereinsidespot(shadowdir, shadowspot, scenter, radius) ? 1 : 0;
        }
    }
    return 0;
}

void shadowmaskbatchedmodels(bool dynshadow)
{
    for(uint i = 0; i < batchedmodels.size(); i++)
    {
        batchedmodel &b = batchedmodels[i];
        if(b.flags&(Model_Mapmodel | Model_NoShadow)) //mapmodels are not dynamic models by definition
        {
            break;
        }
        b.visible = dynshadow && (b.colorscale.a >= 1 || b.flags&(Model_OnlyShadow | Model_ForceShadow)) ? shadowmaskmodel(b.center, b.radius) : 0;
    }
}

int batcheddynamicmodels()
{
    int visible = 0;
    for(uint i = 0; i < batchedmodels.size(); i++)
    {
        batchedmodel &b = batchedmodels[i];
        if(b.flags&Model_Mapmodel) //mapmodels are not dynamic models by definition
        {
            break;
        }
        visible |= b.visible;
    }
    for(uint i = 0; i < batches.size(); i++)
    {
        modelbatch &b = batches[i];
        if(!(b.flags&Model_Mapmodel) || !b.m->animated())
        {
            continue;
        }
        for(int j = b.batched; j >= 0;)
        {
            batchedmodel &bm = batchedmodels[j];
            j = bm.next;
            visible |= bm.visible;
        }
    }
    return visible;
}

int batcheddynamicmodelbounds(int mask, vec &bbmin, vec &bbmax)
{
    int vis = 0;
    for(uint i = 0; i < batchedmodels.size(); i++)
    {
        batchedmodel &b = batchedmodels[i];
        if(b.flags&Model_Mapmodel) //mapmodels are not dynamic models by definition
        {
            break;
        }
        if(b.visible&mask)
        {
            bbmin.min(vec(b.center).sub(b.radius));
            bbmax.max(vec(b.center).add(b.radius));
            ++vis;
        }
    }
    for(uint i = 0; i < batches.size(); i++)
    {
        modelbatch &b = batches[i];
        if(!(b.flags&Model_Mapmodel) || !b.m->animated())
        {
            continue;
        }
        for(int j = b.batched; j >= 0;)
        {
            batchedmodel &bm = batchedmodels[j];
            j = bm.next;
            if(bm.visible&mask)
            {
                bbmin.min(vec(bm.center).sub(bm.radius));
                bbmax.max(vec(bm.center).add(bm.radius));
                ++vis;
            }
        }
    }
    return vis;
}

void rendershadowmodelbatches(bool dynmodel)
{
    for(uint i = 0; i < batches.size(); i++)
    {
        modelbatch &b = batches[i];
        if(!b.m->shadow || (!dynmodel && (!(b.flags&Model_Mapmodel) || b.m->animated())))
        {
            continue;
        }
        bool rendered = false;
        for(int j = b.batched; j >= 0;)
        {
            batchedmodel &bm = batchedmodels[j];
            j = bm.next;
            if(!(bm.visible&(1<<shadowside)))
            {
                continue;
            }
            if(!rendered)
            {
                b.m->startrender();
                rendered = true;
            }
            renderbatchedmodel(b.m, bm);
        }
        if(rendered)
        {
            b.m->endrender();
        }
    }
}

void rendermapmodelbatches()
{
    aamask::enable();
    for(uint i = 0; i < batches.size(); i++)
    {
        modelbatch &b = batches[i];
        if(!(b.flags&Model_Mapmodel))
        {
            continue;
        }
        b.m->startrender();
        aamask::set(b.m->animated());
        for(int j = b.batched; j >= 0;)
        {
            batchedmodel &bm = batchedmodels[j];
            renderbatchedmodel(b.m, bm);
            j = bm.next;
        }
        b.m->endrender();
    }
    aamask::disable();
}

float transmdlsx1 = -1,
      transmdlsy1 = -1,
      transmdlsx2 = 1,
      transmdlsy2 = 1;
uint transmdltiles[lighttilemaxheight];

void rendermodelbatches()
{
    transmdlsx1 = transmdlsy1 = 1;
    transmdlsx2 = transmdlsy2 = -1;
    std::memset(transmdltiles, 0, sizeof(transmdltiles));

    aamask::enable();
    for(uint i = 0; i < batches.size(); i++)
    {
        modelbatch &b = batches[i];
        if(b.flags&Model_Mapmodel)
        {
            continue;
        }
        bool rendered = false;
        for(int j = b.batched; j >= 0;)
        {
            batchedmodel &bm = batchedmodels[j];
            j = bm.next;
            bm.culled = cullmodel(b.m, bm.center, bm.radius, bm.flags, bm.d);
            if(bm.culled || bm.flags&Model_OnlyShadow)
            {
                continue;
            }
            if(bm.colorscale.a < 1 || bm.flags&Model_ForceTransparent)
            {
                float sx1, sy1, sx2, sy2;
                ivec bbmin(vec(bm.center).sub(bm.radius)), bbmax(vec(bm.center).add(bm.radius+1));
                if(calcbbscissor(bbmin, bbmax, sx1, sy1, sx2, sy2))
                {
                    transmdlsx1 = std::min(transmdlsx1, sx1);
                    transmdlsy1 = std::min(transmdlsy1, sy1);
                    transmdlsx2 = std::max(transmdlsx2, sx2);
                    transmdlsy2 = std::max(transmdlsy2, sy2);
                    masktiles(transmdltiles, sx1, sy1, sx2, sy2);
                }
                continue;
            }
            if(!rendered)
            {
                b.m->startrender();
                rendered = true;
                aamask::set(true);
            }
            if(bm.flags&Model_CullQuery)
            {
                bm.d->query = newquery(bm.d);
                if(bm.d->query)
                {
                    bm.d->query->startquery();
                    renderbatchedmodel(b.m, bm);
                    endquery();
                    continue;
                }
            }
            renderbatchedmodel(b.m, bm);
        }
        if(rendered)
        {
            b.m->endrender();
        }
        if(b.flags&Model_CullQuery)
        {
            bool queried = false;
            for(int j = b.batched; j >= 0;)
            {
                batchedmodel &bm = batchedmodels[j];
                j = bm.next;
                if(bm.culled&(Model_CullOccluded|Model_CullQuery) && bm.flags&Model_CullQuery)
                {
                    if(!queried)
                    {
                        if(rendered)
                        {
                            aamask::set(false);
                        }
                        enablecullmodelquery();
                        queried = true;
                    }
                    rendercullmodelquery(b.m, bm.d, bm.center, bm.radius);
                }
            }
            if(queried)
            {
                endbb();
            }
        }
    }
    aamask::disable();
}

void rendertransparentmodelbatches(int stencil)
{
    aamask::enable(stencil);
    for(uint i = 0; i < batches.size(); i++)
    {
        modelbatch &b = batches[i];
        if(b.flags&Model_Mapmodel)
        {
            continue;
        }
        bool rendered = false;
        for(int j = b.batched; j >= 0;)
        {
            batchedmodel &bm = batchedmodels[j];
            j = bm.next;
            bm.culled = cullmodel(b.m, bm.center, bm.radius, bm.flags, bm.d);
            if(bm.culled || !(bm.colorscale.a < 1 || bm.flags&Model_ForceTransparent) || bm.flags&Model_OnlyShadow)
            {
                continue;
            }
            if(!rendered)
            {
                b.m->startrender();
                rendered = true;
                aamask::set(true);
            }
            if(bm.flags&Model_CullQuery)
            {
                bm.d->query = newquery(bm.d);
                if(bm.d->query)
                {
                    bm.d->query->startquery();
                    renderbatchedmodel(b.m, bm);
                    endquery();
                    continue;
                }
            }
            renderbatchedmodel(b.m, bm);
        }
        if(rendered)
        {
            b.m->endrender();
        }
    }
    aamask::disable();
}

static occludequery *modelquery = nullptr;
static int modelquerybatches = -1,
           modelquerymodels = -1,
           modelqueryattached = -1;

void occludequery::startmodelquery()
{
    modelquery = this;
    modelquerybatches = batches.size();
    modelquerymodels = batchedmodels.size();
    modelqueryattached = modelattached.size();
}

void endmodelquery()
{
    if(static_cast<int>(batchedmodels.size()) == modelquerymodels)
    {
        modelquery->fragments = 0;
        modelquery = nullptr;
        return;
    }
    aamask::enable();
    modelquery->startquery();
    for(uint i = 0; i < batches.size(); i++)
    {
        modelbatch &b = batches[i];
        int j = b.batched;
        if(j < modelquerymodels)
        {
            continue;
        }
        b.m->startrender();
        aamask::set(!(b.flags&Model_Mapmodel) || b.m->animated());
        do
        {
            batchedmodel &bm = batchedmodels[j];
            renderbatchedmodel(b.m, bm);
            j = bm.next;
        } while(j >= modelquerymodels);
        b.batched = j;
        b.m->endrender();
    }
    endquery();
    modelquery = nullptr;
    batches.resize(modelquerybatches);
    batchedmodels.resize(modelquerymodels);
    modelattached.resize(modelqueryattached);
    aamask::disable();
}

void clearbatchedmapmodels()
{
    for(uint i = 0; i < batches.size(); i++)
    {
        modelbatch &b = batches[i];
        if(b.flags&Model_Mapmodel)
        {
            batchedmodels.resize(b.batched);
            batches.resize(i);
            break;
        }
    }
}

void rendermapmodel(int idx, int anim, const vec &o, float yaw, float pitch, float roll, int flags, int basetime, float size)
{
    if(!(static_cast<int>(mapmodels.size()) > idx))
    {
        return;
    }
    mapmodelinfo &mmi = mapmodels[idx];
    model *m = mmi.m ? mmi.m : loadmodel(mmi.name);
    if(!m)
    {
        return;
    }
    vec center, bbradius;
    m->boundbox(center, bbradius);
    float radius = bbradius.magnitude();
    center.mul(size);
    if(roll)
    {
        center.rotate_around_y(-roll/RAD);
    }
    if(pitch && m->pitched())
    {
        center.rotate_around_x(pitch/RAD);
    }
    center.rotate_around_z(yaw/RAD);
    center.add(o);
    radius *= size;

    int visible = 0;
    if(shadowmapping)
    {
        if(!m->shadow)
        {
            return;
        }
        visible = shadowmaskmodel(center, radius);
        if(!visible)
        {
            return;
        }
    }
    else if(flags&(Model_CullVFC|Model_CullDist|Model_CullOccluded) && cullmodel(m, center, radius, flags))
    {
        return;
    }
    batchedmodels.emplace_back();
    batchedmodel &b = batchedmodels.back();
    b.pos = o;
    b.center = center;
    b.radius = radius;
    b.anim = anim;
    b.yaw = yaw;
    b.pitch = pitch;
    b.roll = roll;
    b.basetime = basetime;
    b.basetime2 = 0;
    b.sizescale = size;
    b.colorscale = vec4<float>(1, 1, 1, 1);
    b.flags = flags | Model_Mapmodel;
    b.visible = visible;
    b.d = nullptr;
    b.attached = -1;
    addbatchedmodel(m, b, batchedmodels.size()-1);
}

void rendermodel(const char *mdl, int anim, const vec &o, float yaw, float pitch, float roll, int flags, dynent *d, modelattach *a, int basetime, int basetime2, float size, const vec4<float> &color)
{
    model *m = loadmodel(mdl);
    if(!m)
    {
        return;
    }

    vec center, bbradius;
    m->boundbox(center, bbradius);
    float radius = bbradius.magnitude();
    if(d)
    {
        if(d->ragdoll)
        {
            if(anim & Anim_Ragdoll && d->ragdoll->millis >= basetime)
            {
                radius = std::max(radius, d->ragdoll->radius);
                center = d->ragdoll->center;
                goto hasboundbox; //skip roll and pitch stuff
            }
            if(d->ragdoll)
            {
                delete d->ragdoll;
                d->ragdoll = nullptr;
            }
        }
        if(anim & Anim_Ragdoll)
        {
            flags &= ~(Model_CullVFC | Model_CullOccluded | Model_CullQuery);
        }
    }
    center.mul(size);
    if(roll)
    {
        center.rotate_around_y(-roll/RAD);
    }
    if(pitch && m->pitched())
    {
        center.rotate_around_x(pitch/RAD);
    }
    center.rotate_around_z(yaw/RAD);
    center.add(o);
hasboundbox:
    radius *= size;

    if(flags&Model_NoRender)
    {
        anim |= Anim_NoRender;
    }

    if(a)
    {
        for(int i = 0; a[i].tag; i++)
        {
            if(a[i].name)
            {
                a[i].m = loadmodel(a[i].name);
            }
        }
    }

    if(flags&Model_CullQuery)
    {
        if(!oqfrags || !oqdynent || !d)
        {
            flags &= ~Model_CullQuery;
        }
    }

    if(flags&Model_NoBatch)
    {
        int culled = cullmodel(m, center, radius, flags, d);
        if(culled)
        {
            if(culled&(Model_CullOccluded|Model_CullQuery) && flags&Model_CullQuery)
            {
                enablecullmodelquery();
                rendercullmodelquery(m, d, center, radius);
                endbb();
            }
            return;
        }
        aamask::enable();
        if(flags&Model_CullQuery)
        {
            d->query = newquery(d);
            if(d->query)
            {
                d->query->startquery();
            }
        }
        m->startrender();
        aamask::set(true);
        if(flags&Model_FullBright)
        {
            anim |= Anim_FullBright;
        }
        m->render(anim, basetime, basetime2, o, yaw, pitch, roll, d, a, size, color);
        m->endrender();
        if(flags&Model_CullQuery && d->query)
        {
            endquery();
        }
        aamask::disable();
        return;
    }

    batchedmodels.emplace_back();
    batchedmodel & b = batchedmodels.back();
    b.pos = o;
    b.center = center;
    b.radius = radius;
    b.anim = anim;
    b.yaw = yaw;
    b.pitch = pitch;
    b.roll = roll;
    b.basetime = basetime;
    b.basetime2 = basetime2;
    b.sizescale = size;
    b.colorscale = color;
    b.flags = flags;
    b.visible = 0;
    b.d = d;
    b.attached = a ? modelattached.size() : -1;
    if(a)
    {
        for(int i = 0;; i++)
        {
            modelattached.push_back(a[i]);
            if(!a[i].tag)
            {
                break;
            }
        }
    }
    addbatchedmodel(m, b, batchedmodels.size()-1);
}

int intersectmodel(const char *mdl, int anim, const vec &pos, float yaw, float pitch, float roll, const vec &o, const vec &ray, float &dist, int mode, dynent *d, modelattach *a, int basetime, int basetime2, float size)
{
    model *m = loadmodel(mdl);
    if(!m)
    {
        return -1;
    }
    if(d && d->ragdoll && (!(anim & Anim_Ragdoll) || d->ragdoll->millis < basetime))
    {
        if(d->ragdoll)
        {
            delete d->ragdoll;
            d->ragdoll = nullptr;
        }
    }
    if(a)
    {
        for(int i = 0; a[i].tag; i++)
        {
            if(a[i].name)
            {
                a[i].m = loadmodel(a[i].name);
            }
        }
    }
    return m->intersect(anim, basetime, basetime2, pos, yaw, pitch, roll, d, a, size, o, ray, dist, mode);
}

void abovemodel(vec &o, const char *mdl)
{
    model *m = loadmodel(mdl);
    if(!m)
    {
        return;
    }
    o.z += m->above();
}

std::vector<int> findanims(const char *pattern)
{
    std::vector<int> anims;
    for(int i = 0; i < static_cast<int>(animnames.size()); ++i)
    {
        if(!animnames.at(i).compare(pattern))
        {
            anims.push_back(i);
        }
    }
    return anims;
}

void findanimscmd(char *name)
{
    std::vector<int> anims = findanims(name);
    vector<char> buf;
    string num;
    for(int i = 0; i < static_cast<int>(anims.size()); i++)
    {
        formatstring(num, "%d", anims[i]);
        if(i > 0)
        {
            buf.add(' ');
        }
        buf.put(num, std::strlen(num));
    }
    buf.add('\0');
    result(buf.getbuf());
}

//literally goes and attempts a textureload for png, jpg four times using the inside of the if statement

//===================================================================== TRY_LOAD
#define TRY_LOAD(tex, prefix, cmd, name) \
    if((tex = textureload(makerelpath(mdir, name ".jpg", prefix, cmd), 0, true, false))==notexture) \
    { \
        if((tex = textureload(makerelpath(mdir, name ".png", prefix, cmd), 0, true, false))==notexture) \
        { \
            if((tex = textureload(makerelpath(mdir, name ".jpg", prefix, cmd), 0, true, false))==notexture) \
            { \
                if((tex = textureload(makerelpath(mdir, name ".png", prefix, cmd), 0, true, false))==notexture) \
                { \
                    return; \
                } \
            } \
        } \
    }

void loadskin(const char *dir, const char *altdir, Texture *&skin, Texture *&masks) // model skin sharing
{
    DEF_FORMAT_STRING(mdir, "media/model/%s", dir);
    DEF_FORMAT_STRING(maltdir, "media/model/%s", altdir);
    masks = notexture;
    TRY_LOAD(skin, nullptr, nullptr, "skin");
    TRY_LOAD(masks, nullptr, nullptr, "masks");
}

#undef TRY_LOAD
//==============================================================================

void setbbfrommodel(dynent *d, const char *mdl)
{
    model *m = loadmodel(mdl);
    if(!m)
    {
        return;
    }
    vec center, radius;
    m->collisionbox(center, radius);
    if(m->collide != Collide_Ellipse)
    {
        d->collidetype = Collide_OrientedBoundingBox;
    }
    d->xradius   = radius.x + std::fabs(center.x);
    d->yradius   = radius.y + std::fabs(center.y);
    d->radius    = d->collidetype==Collide_OrientedBoundingBox ? sqrtf(d->xradius*d->xradius + d->yradius*d->yradius) : std::max(d->xradius, d->yradius);
    d->eyeheight = (center.z-radius.z) + radius.z*2*m->eyeheight;
    d->aboveeye  = radius.z*2*(1.0f-m->eyeheight);
    if (d->aboveeye + d->eyeheight <= 0.5f)
    {
        float zrad = (0.5f - (d->aboveeye + d->eyeheight)) / 2;
        d->aboveeye += zrad;
        d->eyeheight += zrad;
    }
}

void initrendermodelcmds()
{
    addcommand("mdlcullface", reinterpret_cast<identfun>(mdlcullface), "i", Id_Command);
    addcommand("mdlcolor", reinterpret_cast<identfun>(mdlcolor), "fff", Id_Command);
    addcommand("mdlcollide", reinterpret_cast<identfun>(mdlcollide), "i", Id_Command);
    addcommand("mdlellipsecollide", reinterpret_cast<identfun>(mdlellipsecollide), "i", Id_Command);
    addcommand("mdltricollide", reinterpret_cast<identfun>(mdltricollide), "s", Id_Command);
    addcommand("mdlspec", reinterpret_cast<identfun>(mdlspec), "f", Id_Command);
    addcommand("mdlgloss", reinterpret_cast<identfun>(mdlgloss), "i", Id_Command);
    addcommand("mdlalphatest", reinterpret_cast<identfun>(mdlalphatest), "f", Id_Command);
    addcommand("mdldepthoffset", reinterpret_cast<identfun>(mdldepthoffset), "i", Id_Command);
    addcommand("mdlglow", reinterpret_cast<identfun>(mdlglow), "fff", Id_Command);
    addcommand("mdlfullbright", reinterpret_cast<identfun>(mdlfullbright), "f", Id_Command);
    addcommand("mdlshader", reinterpret_cast<identfun>(mdlshader), "s", Id_Command);
    addcommand("mdlspin", reinterpret_cast<identfun>(mdlspin), "fff", Id_Command);
    addcommand("mdlscale", reinterpret_cast<identfun>(mdlscale), "f", Id_Command);
    addcommand("mdltrans", reinterpret_cast<identfun>(mdltrans), "fff", Id_Command);
    addcommand("mdlyaw", reinterpret_cast<identfun>(mdlyaw), "f", Id_Command);
    addcommand("mdlpitch", reinterpret_cast<identfun>(mdlpitch), "f", Id_Command);
    addcommand("mdlroll", reinterpret_cast<identfun>(mdlroll), "f", Id_Command);
    addcommand("mdlshadow", reinterpret_cast<identfun>(mdlshadow), "i", Id_Command);
    addcommand("mdlalphashadow", reinterpret_cast<identfun>(mdlalphashadow), "i", Id_Command);
    addcommand("mdlbb", reinterpret_cast<identfun>(mdlbb), "fff", Id_Command);
    addcommand("mdlextendbb", reinterpret_cast<identfun>(mdlextendbb), "fff", Id_Command);
    addcommand("mdlname", reinterpret_cast<identfun>(mdlname), "", Id_Command);
    addcommand("rdvert", reinterpret_cast<identfun>(rdvert), "ffff", Id_Command);
    addcommand("rdeye", reinterpret_cast<identfun>(rdeye), "i", Id_Command);
    addcommand("rdtri", reinterpret_cast<identfun>(rdtri), "iii", Id_Command);
    addcommand("rdjoint", reinterpret_cast<identfun>(rdjoint), "iibbb", Id_Command);
    addcommand("rdlimitdist", reinterpret_cast<identfun>(rdlimitdist), "iiff", Id_Command);
    addcommand("rdlimitrot", reinterpret_cast<identfun>(rdlimitrot), "iifffff", Id_Command);
    addcommand("rdanimjoints", reinterpret_cast<identfun>(rdanimjoints), "i", Id_Command);
    addcommand("mapmodelreset", reinterpret_cast<identfun>(mapmodelreset), "i", Id_Command);
    addcommand("mapmodel", reinterpret_cast<identfun>(mapmodel), "s", Id_Command);
    addcommand("mapmodelname", reinterpret_cast<identfun>(mapmodelnamecmd), "ii", Id_Command);
    addcommand("mapmodelloaded", reinterpret_cast<identfun>(mapmodelloaded), "i", Id_Command);
    addcommand("nummapmodels", reinterpret_cast<identfun>(nummapmodels), "", Id_Command);
    addcommand("clearmodel", reinterpret_cast<identfun>(clearmodel), "s", Id_Command);
    addcommand("findanims", reinterpret_cast<identfun>(findanimscmd), "s", Id_Command);
}
