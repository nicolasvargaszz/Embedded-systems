#include "../firmware/WioCoach/CoachCore.h"
#include <cassert>
#include <cstdio>
#include <vector>

using namespace coach;
struct Driver {
  RepCounter c; uint32_t time = 0; std::vector<RepEvent> events;
  explicit Driver(Exercise e) { c.configure(e, profile(e)); }
  void frame(float angle, float torso = 10, bool valid = true) {
    time += 100; auto e = c.update(angle, torso, valid, time);
    if (e.completed || e.rejected) events.push_back(e);
  }
  void hold(float angle, int count = 10, float torso = 10) { while (count--) frame(angle, torso); }
  void ramp(float from, float to, int frames = 15, float torso = 10) {
    for (int i = 1; i <= frames; ++i) frame(from + (to - from) * i / frames, torso);
  }
  void cycle(float rest, float far, float torso = 10) { hold(rest); ramp(rest, far, 15, torso); hold(far, 4, torso); ramp(far, rest, 15, torso); hold(rest); }
};
int main() {
  int checks = 0;
  Point a,b,c; a.x=1; a.y=0; b.x=0; b.y=0; c.x=0; c.y=1;
  a.score=b.score=c.score=100;
  assert(std::fabs(angle(a,b,c)-90)<0.001); ++checks;
  assert(!finite(angle(a,a,c))); ++checks;
  c.score=20; assert(!finite(angle(a,b,c))); ++checks;
  for (auto e : {Exercise::Curl,Exercise::Squat,Exercise::Extension}) {
    Driver d(e); float rest=e==Exercise::Extension?90:170, far=e==Exercise::Curl?40:(e==Exercise::Squat?80:175);
    d.cycle(rest,far); d.cycle(rest,far);
    assert(d.c.good==2 && d.c.bad==0 && d.c.attempts==2); ++checks;
  }
  { Driver d(Exercise::Squat); d.cycle(170,125); assert(d.c.good==0&&d.c.bad==1); assert(d.events[0].faults&Range); ++checks; }
  { Driver d(Exercise::Curl); d.cycle(130,40); assert(d.c.good==0&&d.c.bad==1); assert(d.events[0].faults&RestRange); ++checks; }
  { Driver d(Exercise::Extension); d.cycle(90,135); assert(d.c.good==0&&d.c.bad==1); ++checks; }
  { Driver d(Exercise::Squat); d.cycle(170,80,60); assert(d.c.bad==1 && (d.events[0].faults&Torso)); ++checks; }
  { Driver d(Exercise::Curl); for(int i=0;i<100;++i)d.frame(170+(i%3-1)*2); assert(d.c.attempts==0); ++checks; }
  { Driver d(Exercise::Curl); d.hold(40); d.ramp(40,170); d.hold(170); assert(d.c.attempts==0); ++checks; }
  { Driver d(Exercise::Curl); d.hold(170); d.ramp(170,40); for(int i=0;i<12;++i)d.frame(NAN,10,false); d.ramp(40,170); d.hold(170); assert(d.c.attempts==0); d.cycle(170,40); assert(d.c.good==1); ++checks; }
  { Driver d(Exercise::Curl); d.hold(170); d.ramp(170,40); d.hold(40); assert(d.c.attempts==0); ++checks; }
  { Driver d(Exercise::Curl); d.hold(170); d.ramp(170,40,2); d.ramp(40,170,2); d.hold(170); assert(d.c.good==0); ++checks; }
  { Driver d(Exercise::Squat); d.time=UINT32_MAX-1200; d.cycle(170,80); assert(d.c.good==1); ++checks; }
  { Driver d(Exercise::Curl); d.hold(170); d.ramp(170,40); d.time+=1100; d.ramp(40,170); d.hold(170); assert(d.c.attempts==0); ++checks; }
  // Grove I2C speed: one frame every ~450 ms must still count reps (it used to reset every frame).
  { RepCounter c; c.configure(Exercise::Curl, profile(Exercise::Curl)); uint32_t t = 0; int n = 0;
    const float seq[] = {170,165,172,168,150,110,70,45,40,60,100,140,165,170,168,172,150,100,55,42,80,130,166,171,169};
    for (float v : seq) { t += 450; auto e = c.update(v, 10, true, t); n += e.completed; }
    assert(n == 2 && c.good == 2); ++checks; }
  { Resampler s; Angles a; a.elbow=0; a.knee=90; a.hip=150; a.torso=10;
    std::vector<uint32_t> ts; std::vector<float> v;
    auto emit=[&](uint32_t t,const float* x){ts.push_back(t);v.push_back(x[0]);};
    s.push(50,a,emit); a.elbow=120; s.push(170,a,emit); a.elbow=240; s.push(290,a,emit);
    assert(ts.size()==3 && ts[1]==150 && ts[2]==250); assert(std::fabs(v[1]-100)<0.001); ++checks;
    a.knee=NAN; s.push(300,a,emit); a.knee=90; s.push(500,a,emit); assert(ts.back()==500); ++checks;
  }
  { Pose p; for(int i=0;i<17;++i){p.p[i].x=i%3*30; p.p[i].y=i*20; p.p[i].score=i%2?90:60;}
    assert(bestSide(p,Exercise::Curl,false)==0); p.p[7].score=10; assert(bestSide(p,Exercise::Curl,false)==1); ++checks;
  }
  std::printf("%d core checks passed\n",checks);
}
