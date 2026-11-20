#include <cassert>
#include <cstring>
#include "../firmware/ala_play_games.h"
int main() {
 using namespace espen;
 AlaPlayGames g;
 auto v=g.View();
 assert(v.mode==PlayMode::Menu && v.option_count==3);
 assert(!g.Choose(0) && !g.Next() && !g.Start(PlayMode::Menu,1));
 assert(g.Start(PlayMode::Quiz,1));
 assert(g.View().option_count==3);
 assert(!g.Choose(-1) && !g.Choose(3));
 for(int i=0;i<5;i++){
   v=g.View();assert(v.phase==PlayPhase::Choice && v.round==i+1);
   int correct=-1;
   // Check actual model data (quiz and animals); each question has one right answer.
   if(std::strstr(v.prompt,"7 + 8"))correct=0;
   if(std::strstr(v.prompt,"Słońca"))correct=1;
   if(std::strstr(v.prompt,"pająk"))correct=1;
   if(std::strstr(v.prompt,"nasionka"))correct=0;
   if(std::strstr(v.prompt,"ssakiem"))correct=1;
   if(std::strstr(v.prompt,"minut"))correct=2;
   if(std::strstr(v.prompt,"niebieskiego"))correct=0;
   if(std::strstr(v.prompt,"kontynent"))correct=1;
   if(std::strstr(v.prompt,"trójkąt"))correct=2;
   if(std::strstr(v.prompt,"magnes"))correct=0;
   if(std::strstr(v.prompt,"krew"))correct=1;
   if(std::strstr(v.prompt,"9 razy"))correct=2;
   assert(correct>=0);
   assert(g.Choose(correct) && !g.Choose(correct));
   assert(g.View().phase==PlayPhase::Feedback);
   assert(g.Next() && !g.Next());
 }
 assert(g.View().phase==PlayPhase::Finished && g.View().score==50);
 assert(!g.Choose(0));
 assert(g.Start(PlayMode::Animals,22));
 assert(g.View().option_count==3);
 assert(g.Choose(2) && g.Next());
 assert(g.View().round==2);
 // Story starts by offering three themes, including lake and mom's car.
 for(int theme=0;theme<3;++theme) {
   assert(g.Start(PlayMode::Story,static_cast<uint32_t>(theme+1)));
   v=g.View();
   assert(v.mode==PlayMode::Story && v.option_count==3 && v.round==0);
   assert(std::strcmp(v.options[1],"Nad jeziorem")==0);
   assert(std::strcmp(v.options[2],"Z mamą w samochodzie")==0);
   assert(!g.Choose(-1) && !g.Choose(3));
   assert(g.Choose(theme));
   assert(g.View().option_count==2 && g.View().round==1);
   if(theme==1) assert(std::strstr(g.View().prompt,"jeziorem"));
   if(theme==2) assert(std::strstr(g.View().prompt,"mamą"));
   assert(g.Choose(1) && g.View().phase==PlayPhase::Feedback);
   assert(!g.Choose(0));
   assert(g.Next());
   assert(g.View().option_count==2 && g.View().round==2);
   assert(g.Choose(0) && g.Next());
   assert(g.View().option_count==2 && g.View().round==3);
   assert(g.Choose(1) && g.Next());
   assert(g.View().phase==PlayPhase::Finished && g.View().score==30);
   assert(g.View().prompt && std::strlen(g.View().prompt)>20);
   if(theme==1) assert(std::strstr(g.View().prompt,"jezior")||std::strstr(g.View().prompt,"wysp"));
   if(theme==2) assert(std::strstr(g.View().prompt,"mamą")||std::strstr(g.View().prompt,"Mama"));
   assert(!g.Choose(0));
 }
 // Retain prior forest-branch behavior after theme selection.
 assert(g.Start(PlayMode::Story,1));
 assert(g.Choose(0)); // select forest
 assert(g.Choose(1)&&g.Next());
 assert(std::strstr(g.View().prompt,"rzeką"));
 g.Reset();assert(g.View().mode==PlayMode::Menu);
}
