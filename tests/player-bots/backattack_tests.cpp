#include "player_bot_backattack.h"
#include <cassert>
#include <vector>
using namespace BotBackAttack;
float separation(Point a,Point b){return std::hypot(a.x-b.x,a.y-b.y);}
float nearest(Point a,Point b){float dx=b.x-a.x,dy=b.y-a.y;float t=std::max(0.f,std::min(1.f,-(a.x*dx+a.y*dy)/(dx*dx+dy*dy)));return std::hypot(a.x+t*dx,a.y+t*dy);}
int main(){
 for(float body:{1.f,2.1f,3.5f})for(float enemy:{2.f,6.f,15.f})for(unsigned count=1;count<=5;++count)for(float heading:{0.f,64.f,128.f,192.f,255.f}){
  float r=Radius(enemy,body,count);std::vector<Point> points;
  for(unsigned i=0;i<count;++i){
   Point p=Slot(heading,i,count,r);points.push_back(p);
   float forward=p.x*std::sin(heading*2*Pi/256)+p.y*std::cos(heading*2*Pi/256);
   assert(forward<0);assert(std::hypot(p.x,p.y)>enemy+body);
   for(unsigned j=0;j<i;++j)assert(separation(p,points[j])>=2*body+1.49f);
   Point from{-p.x,-p.y};bool reached=false;
   for(int n=0;n<30;++n){Point next=Step(from,Angle(heading,i,count),r);if(separation(from,next)>.001f)assert(nearest(from,next)>enemy+body);from=next;if(separation(from,p)<.001f){reached=true;break;}}
   assert(reached);
  }
 }
 Point zero=Step({0,0},1.f,12.f);assert(std::isfinite(zero.x)&&std::abs(std::hypot(zero.x,zero.y)-12)<.001f);
 Point outward=Step({0,2},Pi,12);assert(outward.x==0&&outward.y==12);
}
