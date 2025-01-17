#ifndef SHADERPARAM_H_
#define SHADERPARAM_H_

struct UniformLoc
{
    const char *name, *blockname;
    int loc, version, binding, stride, offset, size;
    void *data;
    UniformLoc(const char *name = nullptr, const char *blockname = nullptr, int binding = -1, int stride = -1) : name(name), blockname(blockname), loc(-1), version(-1), binding(binding), stride(stride), offset(-1), size(-1), data(nullptr) {}
};

struct GlobalShaderParamState
{
    const char *name;
    union
    {
        float fval[32];
        int ival[32];
        uint uval[32];
        uchar buf[32*sizeof(float)];
    };
    int version;

    static int nextversion;

    void resetversions();

    void changed()
    {
        if(++nextversion < 0)
        {
            resetversions();
        }
        version = nextversion;
    }
};

struct ShaderParamBinding
{
    int loc, size;
    GLenum format;
};

struct GlobalShaderParamUse : ShaderParamBinding
{

    GlobalShaderParamState *param;
    int version;

    void flush();
};

struct LocalShaderParamState : ShaderParamBinding
{
    const char *name;
};

struct SlotShaderParamState : LocalShaderParamState
{
    int flags;
    float val[4];

    SlotShaderParamState() {}
    SlotShaderParamState(const SlotShaderParam &p)
    {
        name = p.name;
        loc = -1;
        size = 1;
        format = GL_FLOAT_VEC4;
        flags = p.flags;
        std::memcpy(val, p.val, sizeof(val));
    }
};

class Shader
{
    public:
        static Shader *lastshader;

        char *name, *vsstr, *psstr, *defer;
        int type;
        GLuint program, vsobj, psobj;
        std::vector<SlotShaderParamState> defaultparams;
        std::vector<GlobalShaderParamUse> globalparams;
        std::vector<LocalShaderParamState> localparams;
        vector<uchar> localparamremap;
        Shader *variantshader;
        std::vector<Shader *> variants;
        bool standard, forced;
        Shader *reusevs, *reuseps;
        std::vector<UniformLoc> uniformlocs;

        struct AttribLoc
        {
            const char *name;
            int loc;
            AttribLoc(const char *name = nullptr, int loc = -1) : name(name), loc(loc) {}
        };
        std::vector<AttribLoc> attriblocs;
        const void *owner;

        Shader();
        ~Shader();

        void flushparams();
        void force();
        bool invalid() const;
        bool deferred() const;
        bool loaded() const;
        bool isdynamic() const;
        int numvariants(int row) const;
        Shader *getvariant(int col, int row) const;
        void addvariant(int row, Shader *s);
        void setvariant(int col, int row);
        void setvariant(int col, int row, Slot &slot);
        void setvariant(int col, int row, Slot &slot, VSlot &vslot);
        void set();
        void set(Slot &slot);
        void set(Slot &slot, VSlot &vslot);
        bool compile();
        void cleanup(bool full = false);

        static int uniformlocversion();

    private:
        ushort *variantrows;
        bool used;
        void allocparams();
        void setslotparams(Slot &slot);
        void setslotparams(Slot &slot, VSlot &vslot);
        void bindprograms();
        void setvariant_(int col, int row);
        void set_();
};

class GlobalShaderParam
{
    public:
        GlobalShaderParam(const char *name);

        GlobalShaderParamState *resolve();
        void setf(float x = 0, float y = 0, float z = 0, float w = 0);
        void set(const vec &v, float w = 0);
        void set(const vec2 &v, float z = 0, float w = 0);
        void set(const vec4<float> &v);
        void set(const plane &p);
        void set(const matrix2 &m);
        void set(const matrix3 &m);
        void set(const matrix4 &m);
        void seti(int x = 0, int y = 0, int z = 0, int w = 0);
        void set(const ivec &v, int w = 0);
        void set(const ivec2 &v, int z = 0, int w = 0);
        void set(const vec4<int> &v);
        void setu(uint x = 0, uint y = 0, uint z = 0, uint w = 0);

        template<class T>
        T *reserve()
        {
            return (T *)resolve()->buf;
        }
    private:
        const char *name;
        GlobalShaderParamState *param;
};

class LocalShaderParam
{
    public:
        LocalShaderParam(const char *name);

        LocalShaderParamState *resolve();

        void setf(float x = 0, float y = 0, float z = 0, float w = 0);
        void set(const vec &v, float w = 0);
        void set(const vec2 &v, float z = 0, float w = 0);
        void set(const vec4<float> &v);
        void set(const plane &p);
        void setv(const vec *v, int n = 1);
        void setv(const vec2 *v, int n = 1);
        void setv(const vec4<float> *v, int n = 1);
        void setv(const plane *p, int n = 1);
        void setv(const float *f, int n);
        void setv(const matrix2 *m, int n = 1);
        void setv(const matrix3 *m, int n = 1);
        void setv(const matrix4 *m, int n = 1);
        void set(const matrix2 &m);
        void set(const matrix3 &m);
        void set(const matrix4 &m);
        void seti(int x = 0, int y = 0, int z = 0, int w = 0);
        void set(const ivec &v, int w = 0);
        void set(const ivec2 &v, int z = 0, int w = 0);
        void set(const vec4<int> &v);
        void setv(const int *i, int n = 1);
        void setv(const ivec *v, int n = 1);
        void setv(const ivec2 *v, int n = 1);
        void setv(const vec4<int> *v, int n = 1);
        void setu(uint x = 0, uint y = 0, uint z = 0, uint w = 0);
        void setv(const uint *u, int n = 1);
    private:
        const char *name;
        int loc;
};

/**
 * @brief Defines a LocalShaderParam with static storage inside the function's scope
 *
 * This macro creates a LocalShaderParam named `param` and inserts it into the function
 *  as a static variable. This variable cannot be accessed later and remains defined
 * for as long as the program runs.
 *
 * @param name a string (or plain text, that will be stringized)
 * @param vals the values to set, must comply with one of the set() functions for LocalShaderParam
 */
#define LOCALPARAM(name, vals) \
    do \
    { \
        static LocalShaderParam param( #name ); \
        param.set(vals); \
    } \
    while(0)
#define LOCALPARAMF(name, ...) do { static LocalShaderParam param( #name ); param.setf(__VA_ARGS__); } while(0)
#define LOCALPARAMV(name, vals, num) do { static LocalShaderParam param( #name ); param.setv(vals, num); } while(0)
#define GLOBALPARAM(name, vals) do { static GlobalShaderParam param( #name ); param.set(vals); } while(0)
#define GLOBALPARAMF(name, ...) do { static GlobalShaderParam param( #name ); param.setf(__VA_ARGS__); } while(0)
#define GLOBALPARAMV(name, vals, num) do { static GlobalShaderParam param( #name ); param.setv(vals, num); } while(0)

//creates a new static variable inside the function called <name>setshader
//then sets to it any(if present) args passed to set to the shader
//can only be called once per function, and not in the global scope
#define SETSHADER(name, ...) \
    do { \
        static Shader *name##shader = nullptr; \
        if(!name##shader) name##shader = lookupshaderbyname(#name); \
        name##shader->set(__VA_ARGS__); \
    } while(0)
#define SETVARIANT(name, ...) \
    do { \
        static Shader *name##shader = nullptr; \
        if(!name##shader) name##shader = lookupshaderbyname(#name); \
        name##shader->setvariant(__VA_ARGS__); \
    } while(0)

#endif
