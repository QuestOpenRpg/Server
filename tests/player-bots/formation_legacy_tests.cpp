#include "player_bot_formation_navigation.h"
#include <stdexcept>
#include <iostream>
int main() {
    using namespace BotFormation;
    const glm::vec3 a(0,0,3.25f), b(12,0,3.25f);
    auto clear=[](const glm::vec3&,const glm::vec3&){return true;};
    auto floor=[](const glm::vec3&,float& z){z=0;return true;};
    auto check=[](bool ok){if(!ok)throw std::runtime_error("Legacy formation regression");};
    check(SupportedCorridor(a,b,floor,clear));
    check(SupportedCorridor(a,a,floor,clear));
    check(!SupportedCorridor(a,b,floor,[](const glm::vec3&,const glm::vec3&){return false;}));
    check(!SupportedCorridor(a,b,[](const glm::vec3& p,float& z){z=0;return p.x<5 || p.x>7;},clear));
    check(!SupportedCorridor(a,b,[](const glm::vec3& p,float& z){z=p.x<6?0:-12;return true;},clear));
    check(!SupportedCorridor(a,b,[](const glm::vec3& p,float& z){z=0;return p.y<1;},clear));
    check(!SupportedCorridor(a,b,[](const glm::vec3&,float& z){z=-50;return true;},clear));
    check(!SupportedCorridor(a,b,[](const glm::vec3&,float& z){z=10;return true;},clear));
    check(!SupportedCorridor(a,glm::vec3(129,0,3.25f),floor,clear));
    check(SupportedCorridor(a,glm::vec3(12,0,6.25f),[](const glm::vec3& p,float& z){z=p.x*.25f;return true;},clear));
    std::cout << "PASS: legacy formation floor, walls, holes, ledges, side clearance, wrong floors, distance cap, and slope\n";
}
