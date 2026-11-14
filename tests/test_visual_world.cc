#include "../firmware/visual_world.h"
#include <cassert>
#include <iostream>
using namespace espen;
int main(){
    assert(ClassifyScene("Saturn ma pierścienie").scene==Scene::Saturn);
    assert(ClassifyScene("Dinozaury wyginęły").scene==Scene::Dino);
    assert(ClassifyScene("Pszczoły robią miód").scene==Scene::Bee);
    assert(ClassifyScene("Niemcy mają trzy kolory flagi").scene==Scene::Germany);
    assert(ClassifyScene("To jest kotlet").scene==Scene::None);
    auto q=ClassifyScene("7 + 8 = 15");
    assert(q.scene==Scene::Math&&q.a==7&&q.b==8&&q.result==15&&q.op=='+');
    assert(Equation("7 + 8 = 14").scene==Scene::None);
    assert(Equation("3 × 4 = 12").op=='*');
    assert(Equation("12 ÷ 3 = 4").op=='/');
    assert(Equation("2 + 3 = 5.5").scene==Scene::None);
    assert(Equation("1 + 2 + 3 = 5").scene==Scene::None);
    assert(ClassifyScene("To jest ławka").scene==Scene::None);
    assert(!ClassifyScene("7 + 8 = 15",false).op);
    VisualContext context;context.Question("Co to jest Saturn?");context.Begin();
    assert(context.Sentence("To ciekawa planeta.").scene==Scene::Space);
    assert(context.Sentence("Saturn ma pierścienie.").scene==Scene::Saturn);
    assert(context.Sentence("Składają się z lodu.").scene==Scene::Ice);
    assert(context.Sentence("To naprawdę ciekawe.").scene==Scene::Ice);
    assert(context.Sentence("Można je zobaczyć.").scene==Scene::Ice);
    assert(context.Sentence("To wszystko.").scene==Scene::Talk);
    for(int i=1;i<static_cast<int>(Scene::Count);++i){
        auto scene=static_cast<Scene>(i);assert(*Caption(scene));
        for(uint32_t t=0;t<5000;t+=125){auto f=DrawVisual({scene},t);assert(f.count>0&&!f.overflow);
            for(int j=0;j<f.count;++j){auto p=f.items[j];assert(p.x>=0&&p.y>=0&&p.x<=125&&p.y<=125);
                if(!p.line)assert(p.w>0&&p.h>0&&p.x+p.w<=126&&p.y+p.h<=126);
                else assert(p.w>=0&&p.w<=125&&p.h>=0&&p.h<=125);
            }
        }
    }
    assert(!DrawVisual({Scene::Math,6,6,36,'*'},1000).overflow);
    HoldGate h;h.Begin(20,30,100);assert(!h.Ready(5099));assert(h.Ready(5100));assert(!h.Ready(6100));
    h.Begin(20,30,7000);h.Move(40,30);assert(!h.Ready(12001));
    h.Begin(20,30,13000);h.Cancel();assert(!h.Ready(19000));
    h.Begin(20,30,0xffffff00u);assert(h.Ready(0xffffff00u+5000u));
    std::cout<<"Espen visual world: "<<static_cast<int>(Scene::Count)-1<<" scenes and 5-second hold tests passed\n";
}
