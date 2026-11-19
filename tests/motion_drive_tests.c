/* 纯C运动回归，只提供用例；本轮不调用编译器。 */
#include "motion_math.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void reverse_timing(float sign)
{
    uint32_t wait=0;
    float current=sign*40.0f;
    for(int i=0;i<5;++i)
        current=motion_math_ramp_motor(current,-sign*40.0f,10.0f,8.0f,20,80,&wait);
    assert(current==0 && wait==80);
    for(int i=0;i<3;++i) {
        current=motion_math_ramp_motor(current,-sign*40.0f,10.0f,8.0f,20,80,&wait);
        assert(current==0 && wait==(uint32_t)(60-i*20));
    }
    current=motion_math_ramp_motor(current,-sign*40.0f,10.0f,8.0f,20,80,&wait);
    assert(current==-sign*10.0f && wait==0); // 零区完整80ms后才反向加速。
}

int main(void)
{
    reverse_timing(1.0f); reverse_timing(-1.0f);
    uint32_t wait=80;
    assert(motion_math_ramp_motor(0,0,10,8,20,80,&wait)==0 && wait==0);
    wait=80;
    float current=0;
    for(int i=0;i<3;++i) {
        current=motion_math_ramp_motor(current,40,10,8,20,80,&wait);
        assert(current==0);
    }
    current=motion_math_ramp_motor(current,40,10,8,20,80,&wait);
    assert(current==10 && wait==0); // 等待中改主意也不会残留旧方向。
    assert(motion_math_breakaway_min(0,18,40,0.08f)==18);
    assert(fabsf(motion_math_breakaway_min(0.04f,18,40,0.08f)-29)<0.001f);
    assert(motion_math_breakaway_min(0.08f,18,40,0.08f)==40);
    assert(motion_math_normalized_to_percent(0,40,100)==0);
    assert(motion_math_normalized_to_percent(0.1f,40,100)==46);
    assert(motion_math_normalized_to_percent(-0.1f,40,100)==-46);
    assert(motion_math_normalized_to_percent(1,40,100)==100);
    assert(motion_math_normalized_to_percent(-1,40,100)==-100);
    const motion_twist_t directions[]={{1,0},{-1,0},{0,1},{0,-1}};
    for(unsigned i=0;i<4;++i) {
        motion_wheel_mix_t wheels;
        motion_math_mix_wheels(directions[i].linear,directions[i].angular,-1,1,1,&wheels);
        assert(fabsf(wheels.motor_a)==1 && fabsf(wheels.motor_b)==1);
    }
    puts("motion drive cases passed");
    return 0;
}
