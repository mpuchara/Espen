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
 assert(g.Start(PlayMode::Story,1));
 assert(g.View().option_count==2);
 assert(g.Choose(1)&&g.View().phase==PlayPhase::Feedback&&g.Next());
 auto river=g.View();assert(std::strstr(river.prompt,"rzeką"));
 assert(g.Choose(0)&&g.Next());
 assert(g.View().option_count==2);
 assert(g.Choose(1)&&g.Next());
 assert(g.View().phase==PlayPhase::Finished&&g.View().score==30);
 g.Reset();assert(g.View().mode==PlayMode::Menu);
}
