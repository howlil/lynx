#pragma once
// HTML v3 visual contract for the native GDI+ host. Rendering only: never
// mutates coordinator, timers, media provider or platform services.
#include "../core/v3_presentation.hpp"
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace lynx::v3 {
using namespace Gdiplus;

struct PaintState {
    Layout layout{};
    View view=View::Ambient;
    CenterMode center=CenterMode::Lens;
    bool charging=false, batteryKnown=false, playing=false;
    int battery=0, focusRemaining=1500, focusDuration=1500;
    std::int64_t mediaPositionMs=0,mediaDurationMs=0;
    bool canPrevious=false,canToggle=false,canNext=false;
    bool hasSecondary=false;
    std::wstring time=L"9:41",day=L"SUN",title,subtitle,notice;
    std::wstring secondaryLabel;
    int focused=-1;
};

inline Color white(){return Color(255,245,245,247);}
inline Color secondary(){return Color(255,157,157,164);}
inline Color orange(){return Color(255,255,159,10);}
inline Color blue(){return Color(255,10,132,255);}

inline void text(Graphics& g,const std::wstring& str,float px,float x,float y,
                 Color color=white(),bool heavy=false) {
    Font f(L"Segoe UI",px,heavy?FontStyleBold:FontStyleRegular,UnitPixel);
    SolidBrush b(color);
    StringFormat fmt;
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(str.c_str(),-1,&f,PointF(x,y),&fmt,&b);
}
inline void pill(GraphicsPath& path,float x,float y,float w,float h,float r) {
    r=std::clamp(r,0.5f,std::min(w,h)*.5f);
    const float d=r*2;
    path.StartFigure();
    path.AddArc(x+w-d,y,d,d,270,90);
    path.AddArc(x+w-d,y+h-d,d,d,0,90);
    path.AddArc(x,y+h-d,d,d,90,90);
    path.AddArc(x,y,d,d,180,90);
    path.CloseFigure();
}
inline void fillRound(Graphics& g,float x,float y,float w,float h,float r,Color c){
    GraphicsPath p;pill(p,x,y,w,h,r);SolidBrush b(c);g.FillPath(&b,&p);
}
inline void strokeRound(Graphics& g,float x,float y,float w,float h,float r,Color c,float line=1){
    GraphicsPath p;pill(p,x,y,w,h,r);Pen pen(c,line);g.DrawPath(&pen,&p);
}
inline void line(Graphics& g,float x1,float y1,float x2,float y2,Color c,float width=1.8f){
    Pen p(c,width);p.SetStartCap(LineCapRound);p.SetEndCap(LineCapRound);
    g.DrawLine(&p,x1,y1,x2,y2);
}
inline void triangle(Graphics& g,PointF a,PointF b,PointF c,Color color){
    PointF pts[3]{a,b,c};SolidBrush brush(color);g.FillPolygon(&brush,pts,3);
}
inline void drawPlay(Graphics& g,float cx,float cy,bool playing,Color col=white()){
    if(playing){
        fillRound(g,cx-7,cy-10,5,20,1.5f,col);
        fillRound(g,cx+2,cy-10,5,20,1.5f,col);
    }else{
        triangle(g,{cx-6,cy-11},{cx+11,cy},{cx-6,cy+11},col);
    }
}
inline void drawSkip(Graphics& g,float cx,float cy,bool next,bool enabled){
    const Color c=enabled?white():Color(255,90,90,96);
    const float s=next?1.f:-1.f;
    triangle(g,{cx-9*s,cy-9},{cx+5*s,cy},{cx-9*s,cy+9},c);
    line(g,cx+7*s,cy-9,cx+7*s,cy+9,c,2.5f);
}
inline void drawBattery(Graphics& g,float x,float y,int percent,Color c) {
    strokeRound(g,x,y,16,10,2.2f,c,1.2f);
    fillRound(g,x+17,y+3,2,4,0.8f,c);
    fillRound(g,x+2,y+2,11.f*std::clamp(percent,0,100)/100.f,6,1,c);
}
inline void drawMusic(Graphics& g,float x,float y,Color color=white()){
    line(g,x+4,y+2,x+4,y+17,color,2);
    line(g,x+4,y+2,x+15,y,color,2);
    line(g,x+15,y,x+15,y+14,color,2);
    SolidBrush br(color);
    g.FillEllipse(&br,x-1,y+14,7,6);
    g.FillEllipse(&br,x+10,y+11,7,6);
}
inline void drawTimer(Graphics& g,float x,float y,Color c=orange()){
    Pen p(c,1.7f);g.DrawEllipse(&p,x+2,y+3,15,15);
    line(g,x+10,y+10,x+13,y+7,c,1.6f);
    line(g,x+7,y,x+12,y,c,1.6f);
}
inline void waveform(Graphics& g,float x,float y,bool playing){
    const float heights[5]={5,11,16,8,12};
    const Color c=Color(255,255,140,88);
    for(int i=0;i<5;i++){
        const float scale=playing?0.72f+0.28f*
             std::sin(static_cast<float>(GetTickCount64()/110+i*2)):0.35f;
        const float h=std::max(3.f,heights[i]*scale);
        fillRound(g,x+i*4,y-h*.5f,2,h,1,c);
    }
}
inline std::wstring stamp(int seconds) {
    seconds=std::max(0,seconds);
    const int minutes=seconds/60,secs=seconds%60;
    return std::to_wstring(minutes)+L":"+(secs<10?L"0":L"")+std::to_wstring(secs);
}
inline void ambient(Graphics& g,const PaintState& p,float x,float y,float w,float h) {
    if(p.layout.handle) {
        if(p.layout.geometry.width<70)fillRound(g,x+w*.5f-8,y+h*.5f-1,16,2,1,secondary());
        return;
    }
    const float sh=p.layout.geometry.shoulder;
    text(g,p.time,14,x+sh+14,y+10,white(),true);
    const float mid=x+w*.5f;
    if(p.center==CenterMode::Lens){
        fillRound(g,mid-17,y+16,34,10,5,Color(255,16,16,20));
        strokeRound(g,mid-17,y+16,34,10,5,Color(255,35,35,39),.8f);
    } else if(p.center==CenterMode::Dot) {
        SolidBrush dot(Color(255,52,199,89));g.FillEllipse(&dot,mid-3,y+18,6,6);
    } else {
        text(g,p.day,9,mid-10,y+15,secondary(),true);
    }
    if(p.batteryKnown){
        const Color c=p.battery<=20 && !p.charging?orange():
            p.charging?Color(255,48,209,88):white();
        text(g,std::to_wstring(p.battery)+L"%",11,x+w-sh-70,y+13,c,true);
        drawBattery(g,x+w-sh-34,y+17,p.battery,c);
    }else{
        text(g,L"AC",11,x+w-sh-44,y+12,secondary());
    }
}
inline void compact(Graphics& g,const PaintState& p,float x,float y,float w,float h) {
    const float sh=p.layout.geometry.shoulder;
    const float lx=x+sh+13;
    if(p.view==View::Transient) {
        const bool warn=p.notice.find(L"Low")!=std::wstring::npos;
        const Color c=warn?orange():Color(255,48,209,88);
        fillRound(g,lx,y+11,23,23,11.5f,Color(255,32,32,36));
        if(warn) drawBattery(g,lx+4,y+18,20,c);
        else {line(g,lx+6,y+22,lx+10,y+26,c,2);
              line(g,lx+10,y+26,lx+17,y+17,c,2);}
        const std::wstring msg=p.notice.empty()?L"Notification":p.notice;
        text(g,msg,11,lx+30,y+17,c,true);
        return;
    }
    if(p.layout.scene==Scene::Media) {
        fillRound(g,lx,y+8,22,22,6,Color(255,95,90,150));
        drawMusic(g,lx+5,y+10,white());
        text(g,p.title.empty()?L"Now Playing":p.title,11,lx+30,y+10,white(),true);
        waveform(g,x+w-sh-31,y+19,p.playing);
    } else if(p.layout.scene==Scene::Timer) {
        drawTimer(g,lx+1,y+8,orange());
        text(g,L"Focus timer",11,lx+23,y+10,white(),true);
        text(g,stamp(p.focusRemaining),11,x+w-sh-52,y+10,orange(),true);
    } else if(p.layout.scene==Scene::Shelf) {
        text(g,L"File Shelf",11,lx+3,y+10,white(),true);
        text(g,p.title,10,x+w-sh-94,y+10,secondary());
    } else {
        ambient(g,p,x,y,w,h);
    }
}
inline void media(Graphics& g,const PaintState& p,float x,float y,float w,float h) {
    const float sh=p.layout.geometry.shoulder;
    const float left=x+sh+16;
    fillRound(g,left,y+15,53,53,12,Color(255,115,88,148));
    // Stylized record-like album art, matching HTML v3 illustrative art
    Pen art(Color(100,255,210,200),6);g.DrawEllipse(&art,left+15,y+8,46,46);
    Pen arc(Color(100,212,174,233),5);g.DrawEllipse(&arc,left-12,y+38,51,51);
    drawMusic(g,left+20,y+31,white());
    text(g,p.title.empty()?L"Now Playing":p.title,13,left+64,y+21,white(),true);
    text(g,p.subtitle,10,left+64,y+43,secondary());
    waveform(g,x+w-sh-35,y+41,p.playing);
    const float trackX=left,trackW=w-2*sh-32;
    fillRound(g,trackX,y+80,trackW,4,2,Color(255,59,59,64));
    const float ratio=progressRatio(p.mediaPositionMs,p.mediaDurationMs);
    if(ratio>0)fillRound(g,trackX,y+80,std::max(2.f,trackW*ratio),4,2,white());
    const auto toSeconds=[](std::int64_t v)->int {
        return static_cast<int>(std::clamp<std::int64_t>(v/1000,0,86399));
    };
    text(g,stamp(toSeconds(p.mediaPositionMs)),9,trackX,y+88,secondary());
    const std::wstring total=p.mediaDurationMs>0 ?
        stamp(toSeconds(p.mediaDurationMs)) : L"--:--";
    text(g,total,9,trackX+trackW-34,y+88,secondary());
    for(int i=0;i<3;i++){
        auto b=p.layout.actions[i];
        const float cx=x+b.x+b.w*.5f,cy=y+b.y+b.h*.5f;
        if(i==0)drawSkip(g,cx,cy,false,p.canPrevious);
        else if(i==1)drawPlay(g,cx,cy,p.playing,p.canToggle?white():secondary());
        else drawSkip(g,cx,cy,true,p.canNext);
    }
}
inline void timer(Graphics& g,const PaintState& p,float x,float y,float w,float h) {
    drawTimer(g,x+17,y+14,orange());
    text(g,L"Focus Timer",11,x+39,y+17,secondary(),true);
    const float cx=x+w*.5f,cy=y+77.f;
    Pen track(Color(255,45,45,50),3.6f);
    g.DrawEllipse(&track,cx-40,cy-40,80,80);
    Pen active(orange(),3.6f);active.SetStartCap(LineCapRound);active.SetEndCap(LineCapRound);
    const float progress=focusProgress(p.focusRemaining,p.focusDuration);
    if(progress>.001f)g.DrawArc(&active,cx-40,cy-40,80,80,-90.f,360.f*progress);
    const auto value=stamp(p.focusRemaining);
    text(g,value,23,cx-29,cy-17,white(),true);
    for(int i=0;i<2;i++){
        auto b=p.layout.actions[i];
        const float bx=x+b.x,by=y+b.y;
        fillRound(g,bx,by,b.w,b.h,b.w*.5f,
                  i==0?Color(255,37,37,43):Color(255,94,64,14));
        if(i==0){line(g,bx+14,by+13,bx+25,by+25,white());
                 line(g,bx+25,by+13,bx+14,by+25,white());}
        else drawPlay(g,bx+b.w*.5f,by+b.h*.5f,
                      p.playing,Color(255,255,179,64));
    }
}
inline void idleExpanded(Graphics& g,const PaintState& p,float x,float y,float w) {
    const float cx=x+w*.5f;
    text(g,p.time,19,cx-24,y+35,white(),true);
    text(g,p.day+L"  ·  Battery "+std::to_wstring(p.battery)+L"%",
        11,cx-74,y+66,secondary());
    text(g,L"All quiet. Nothing needs your attention.",11,cx-111,y+87,secondary());
    fillRound(g,cx-43,y+114,86,29,15,Color(255,36,36,43));
    text(g,L"Start Focus",10,cx-29,y+121,white(),true);
}
inline void shelf(Graphics& g,const PaintState& p,float x,float y,float w) {
    text(g,L"File Shelf",15,x+23,y+24,white(),true);
    text(g,p.title,11,x+23,y+57,secondary());
    text(g,L"References only · files stay in their folders",10,x+23,y+83,secondary());
    for(int i=0;i<2;i++){
        const auto b=p.layout.actions[i];
        fillRound(g,x+b.x,y+b.y,b.w,b.h,13,Color(255,37,37,43));
        text(g,i==0?L"Reveal":L"Clear",11,x+b.x+22,y+b.y+8,white(),true);
    }
}
inline void alert(Graphics& g,const PaintState& p,float x,float y,float w) {
    const bool warn=p.notice.find(L"Low")!=std::wstring::npos;
    const Color c=warn?orange():Color(255,48,209,88);
    fillRound(g,x+16,y+27,45,45,23,Color(255,33,33,37));
    if(warn) drawBattery(g,x+28,y+44,18,c);
    else {line(g,x+30,y+51,x+37,y+57,c,2.5f);
          line(g,x+37,y+57,x+49,y+42,c,2.5f);}
    text(g,p.notice,14,x+74,y+28,white(),true);
    text(g,L"Notification from Windows",11,x+74,y+53,secondary());
}
inline void paint(Graphics& graphics,const PaintState& state,
                  float x,float y,float width,float height) {
    const auto view=state.view;
    if(state.layout.handle) {
        ambient(graphics,state,x,y,width,height);
    } else if(view==View::Ambient) {
        ambient(graphics,state,x,y,width,height);
    } else if(view==View::Compact || view==View::Transient) {
        compact(graphics,state,x,y,width,height);
    } else if(view==View::Expanded) {
        switch(state.layout.scene) {
            case Scene::Media:media(graphics,state,x,y,width,height);break;
            case Scene::Timer:timer(graphics,state,x,y,width,height);break;
            case Scene::Shelf:shelf(graphics,state,x,y,width);break;
            case Scene::Alert:alert(graphics,state,x,y,width);break;
            default:idleExpanded(graphics,state,x,y,width);break;
        }
    }
    if(view==View::Expanded && state.focused>=0 && state.focused<state.layout.count) {
        const auto b=state.layout.actions[state.focused];
        strokeRound(graphics,x+b.x-2,y+b.y-2,b.w+4,b.h+4,
                    std::min(b.w,b.h)*.45f,blue(),1.7f);
    }
}
inline void secondaryBubble(Graphics& graphics,float x,float y,bool focusSecondary) {
    fillRound(graphics,x,y,36,36,18,Color(255,0,0,0));
    if(focusSecondary)drawTimer(graphics,x+9,y+9,orange());
    else drawMusic(graphics,x+9,y+8,orange());
}
} // namespace lynx::v3
