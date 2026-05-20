#include "IScript.h"
#include <iostream>
#include "baseobject.h"
using namespace HonHengine;
class MyScript : public IScript {
public:
    float speed = 5.0f; int count = 0;
    void Start() override { std::cout << "MyScript started" << std::endl; }
    void Update(float dt) override { 
    }
    void OnDestroy() override { std::cout << "MyScript destroyed" << std::endl; }
};
extern "C" IScript* CreateScript() { return new MyScript(); }
extern "C" void DestroyScript(IScript* s) { delete s; }
