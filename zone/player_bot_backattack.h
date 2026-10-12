#pragma once
#include <algorithm>
#include <cmath>
namespace BotBackAttack {
constexpr float Pi=3.14159265358979323846f;
struct Point {float x,y;};
inline unsigned Rank(unsigned index){return (index+1)/2;}
inline float Radius(float enemy_radius,float body_radius,unsigned count){
 const unsigned ranks=std::max(1u,count/2);
 const float step=std::min(Pi/6, (70.f*Pi/180)/ranks);
 return std::max(enemy_radius+body_radius+2.f,(2*body_radius+1.5f)/(2*std::sin(step/2)));
}
inline float Angle(float heading,unsigned index,unsigned count){
 const float step=std::min(Pi/6,(70.f*Pi/180)/std::max(1u,count/2));
 return heading*(2*Pi/256)+Pi+(index%2?-1.f:1.f)*static_cast<float>(Rank(index))*step;
}
inline Point Slot(float heading,unsigned index,unsigned count,float radius){
 const float angle=Angle(heading,index,count);return {std::sin(angle)*radius,std::cos(angle)*radius};
}
// Follow an arc in short straight segments instead of crossing the enemy.
inline Point Step(Point from,float destination_angle,float radius){
 const float distance=std::hypot(from.x,from.y);
 float angle=distance>.01f?std::atan2(from.x,from.y):destination_angle;
 if(distance<radius-.25f)return {std::sin(angle)*radius,std::cos(angle)*radius};
 const float change=std::remainder(destination_angle-angle,2*Pi);
 angle+=std::max(-Pi/9,std::min(Pi/9,change));
 return {std::sin(angle)*radius,std::cos(angle)*radius};
}
}
