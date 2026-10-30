#include "renderer/cog_pyr_math.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace jkgm;

static bool near(float a,float b,float tol=0.00015f)
{
    return std::fabs(a-b)<tol;
}
static void matches(cog_pyr_vec const &a,cog_pyr_vec const &b,
                    float tolerance=0.00015f)
{
    for(int i=0;i<3;++i)assert(near(a[i],b[i],tolerance));
}
static void check_direction(cog_pyr_vec direction,cog_pyr_axis axis)
{
    cog_pyr_vec normalized=direction;
    assert(cog_pyr_normalize(normalized));
    cog_pyr_vec const pyr=cog_pyr_from_direction(direction,axis);
    matches(cog_pyr_direction(pyr,axis),normalized,0.0002f);
    if(axis==cog_pyr_axis::look)assert(pyr[2]==0.0f);
}
int main()
{
    matches(cog_pyr_direction({{0,0,0}},cog_pyr_axis::right),{{1,0,0}});
    matches(cog_pyr_direction({{0,0,0}},cog_pyr_axis::look),{{0,1,0}});
    matches(cog_pyr_direction({{0,0,0}},cog_pyr_axis::up),{{0,0,1}});
    matches(cog_pyr_direction({{90,0,0}},cog_pyr_axis::look),{{0,0,1}});
    matches(cog_pyr_direction({{-90,0,0}},cog_pyr_axis::look),{{0,0,-1}});
    matches(cog_pyr_direction({{0,90,0}},cog_pyr_axis::look),{{-1,0,0}});
    matches(cog_pyr_direction({{0,0,90}},cog_pyr_axis::right),{{0,0,-1}});
    matches(cog_pyr_direction({{0,0,90}},cog_pyr_axis::up),{{1,0,0}});
    for(cog_pyr_axis axis:{
        cog_pyr_axis::look,cog_pyr_axis::right,cog_pyr_axis::up}) {
        for(cog_pyr_vec v:{
            cog_pyr_vec{{1,0,0}},{{0,1,0}},{{0,0,1}},
            {{-1,0,0}},{{0,-1,0}},{{0,0,-1}},
            {{1,2,3}},{{-3,-5,1}},{{0.001f,0.002f,1}},
            {{0.0001f,-1,0.00003f}}})
            check_direction(v,axis);
        for(int a=-5;a<=5;++a)for(int b=-5;b<=5;++b)
          for(int c=-5;c<=5;++c) {
            if(a==0 && b==0 && c==0)continue;
            check_direction({{float(a),float(b),float(c)}},axis);
        }
    }
    for(int p=-90;p<=90;p+=15)for(int y=-180;y<=180;y+=30)
      for(int r=-180;r<=180;r+=45) {
        cog_pyr_vec const pyr{{float(p),float(y),float(r)}};
        cog_pyr_basis basis{};
        assert(cog_pyr_build(pyr,basis));
        cog_pyr_vec const lookPyr=cog_pyr_from_direction(
            basis.look,cog_pyr_axis::look);
        assert(lookPyr[2]==0.0f);
        matches(cog_pyr_direction(lookPyr,cog_pyr_axis::look),basis.look);
        check_direction(basis.right,cog_pyr_axis::right);
        check_direction(basis.up,cog_pyr_axis::up);
    }
    // RotateVector accepts displacements of any length, not just LVecs.
    matches(cog_pyr_rotate_vector({{0,4,0}},{{90,0,0}}),
            {{0,0,4}});
    matches(cog_pyr_rotate_vector({{0,4,0}},{{-90,0,0}}),
            {{0,0,-4}});
    matches(cog_pyr_rotate_vector({{0,4,0}},{{0,90,0}}),
            {{-4,0,0}});
    matches(cog_pyr_rotate_vector({{2,-5,7}},{{0,0,0}}),
            {{2,-5,7}});
    matches(cog_pyr_rotate_vector({{0,0,0}},{{25,-40,70}}),
            {{0,0,0}});
    matches(cog_pyr_rotate_vector({{2,3,4}},{{0,0,90}}),
            {{4,3,-2}});
    // Rotation changes direction, not the vector's magnitude.
    for(cog_pyr_vec v: {cog_pyr_vec{{0,4,0}},{{2,-3,7}},{{-11,5,0.25f}}}) {
        float const len2=cog_pyr_dot(v,v);
        for(cog_pyr_vec pyr: {cog_pyr_vec{{30,45,60}},
                              {{-80,270,-35}},{{90,0,0}}}) {
            auto const out=cog_pyr_rotate_vector(v,pyr);
            assert(near(cog_pyr_dot(out,out),len2,0.001f));
        }
    }
    float const nan=std::numeric_limits<float>::quiet_NaN();
    matches(cog_pyr_from_direction({{0,0,0}},cog_pyr_axis::look),{{0,0,0}});
    matches(cog_pyr_from_direction({{nan,1,0}},cog_pyr_axis::right),{{0,0,0}});
    matches(cog_pyr_direction({{0,nan,0}},cog_pyr_axis::up),{{0,0,0}});
    matches(cog_pyr_rotate_vector({{0,4,0}},{{nan,0,0}}),
            {{0,0,0}});
    matches(cog_pyr_rotate_vector({{nan,4,0}},{{0,0,0}}),
            {{0,0,0}});
}
