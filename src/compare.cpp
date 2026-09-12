#include "compare.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <numbers>
#include <ostream>
#include <sstream>
#include <stdexcept>

namespace pt {
std::array<Candidate,candidateCount> candidates(LocalOptions o) {
    return {{{"local_custom","Local adjustable",Algorithm::Local,o.radius,o.window,o.cap},
        {"euro_responsive","One Euro responsive",Algorithm::OneEuro,0,0,6,3,.020,12},
        {"euro_smooth","One Euro smooth",Algorithm::OneEuro,0,0,10,1,.005,8},
        {"buffer80","Buffered average 80 ms",Algorithm::Buffered,0,.080,10},
        {"offline120","Offline Gaussian 120 ms",Algorithm::OfflineGaussian,0,.120,4}}};
}
std::vector<Candidate> sweepCandidates() {
    const auto base=candidates(); std::vector<Candidate> result(base.begin(),base.end());
    for(double window:{.040,.080,.120,.200}) for(double radius:{4.0,8.0,12.0})
        result.push_back({"local_"+std::to_string(static_cast<int>(window*1000))+"_"+std::to_string(static_cast<int>(radius)),
            "Local sweep",Algorithm::Local,radius,window,4});
    for(double cutoff:{.5,1.0,3.0}) for(double beta:{.005,.020})
        result.push_back({"euro_"+std::to_string(cutoff)+"_"+std::to_string(beta),"One Euro sweep",Algorithm::OneEuro,0,0,10,cutoff,beta,12});
    for(double window:{.040,.160}) result.push_back({"buffer_"+std::to_string(static_cast<int>(window*1000)),
        "Buffered sweep",Algorithm::Buffered,0,window,10});
    return result;
}
std::string comparisonSettings(LocalOptions o) {
    std::ostringstream text; text.imbue(std::locale::classic()); text<<std::setprecision(17)
        <<"Comparison v0.4.0 local "<<o.radius<<' '<<o.window<<' '<<o.cap; return text.str();
}
LocalOptions recordedComparisonSettings(const Session& session) {
    LocalOptions result;
    const std::string prefix="Comparison v0.4.0 local ", previous="Comparison v0.3.0 local ";
    for(const auto& e:session.events) if(e.text.starts_with(prefix) || e.text.starts_with(previous)) {
        LocalOptions o; std::istringstream in(e.text.substr(prefix.size())); in.imbue(std::locale::classic());
        if(!(in>>o.radius>>o.window>>o.cap)) continue;
        in>>std::ws;
        if(in.eof() && std::isfinite(o.radius) && std::isfinite(o.window) && std::isfinite(o.cap) &&
            o.radius>0 && o.radius<=100 && o.window>0 && o.window<=1 && o.cap>0 && o.cap<=50) result=o;
    }
    return result;
}
const char* executionKind(Algorithm a) {
    if(a==Algorithm::Local) return "bounded_revision";
    if(a==Algorithm::OfflineGaussian) return "finished_only";
    return "causal";
}
namespace {
double alpha(double cutoff,double dt) { const double r=2*std::numbers::pi*cutoff*dt; return r/(1+r); }
Vec capped(Vec result,Vec raw,double cap) {
    const auto d=result-raw; const double n=length(d);
    return n>cap?raw+d*(cap/n):result;
}
struct Run {
    std::vector<double> times;
    std::vector<Vec> points,area;
    Run(const Stroke& stroke,std::size_t first,std::size_t end) {
        for(auto i=first;i<end;++i) {
            times.push_back(stroke.points[i].time); points.push_back(stroke.points[i].p);
            area.push_back(i==first?Vec{}:area.back()+(points.back()+points[points.size()-2])*
                ((times.back()-times[times.size()-2])*.5));
        }
    }
    std::pair<Vec,Vec> at(double t) const {
        if(t<=times.front()) return {points.front(),area.front()};
        if(t>=times.back()) return {points.back(),area.back()};
        const auto hi=static_cast<std::size_t>(std::upper_bound(times.begin(),times.end(),t)-times.begin());
        const double dt=t-times[hi-1],fraction=dt/(times[hi]-times[hi-1]);
        const auto p=points[hi-1]+(points[hi]-points[hi-1])*fraction;
        return {p,area[hi-1]+(p+points[hi-1])*(dt*.5)};
    }
};
double polygonArea(const std::vector<Vec>& p) {
    if(p.size()<3) return 0;
    double area=0; const auto origin=p.front();
    for(std::size_t i=1;i+1<p.size();++i) {
        const auto a=p[i]-origin,b=p[i+1]-origin; area+=a.x*b.y-a.y*b.x;
    }
    return area*.5;
}
void scoreExtras(Comparison& c,const Stroke& stroke,const std::vector<Vec>& raw,const std::vector<Motion>& movement) {
    bool clean=true; for(std::size_t i=1;i<movement.size();++i) if(!movement[i].valid()) clean=false;
    if(clean) for(unsigned i=0;i<3;++i) c.variation[i]=localVariation(c.path,5.0*(1u<<i));
    std::vector<double> lags; std::size_t start=0;
    for(std::size_t i=1;i<raw.size();++i) {
        if(!movement[i].valid()) { start=i; continue; }
        if(movement[i].speed<5) continue; // Near-stationary nearest-path time is ambiguous.
        double best=length(c.path[i]-raw[i]),bestTime=stroke.points[i].time;
        for(std::size_t j=i;j>start && i-j<128;--j) {
            const double ta=stroke.points[j-1].time,tb=stroke.points[j].time;
            if(stroke.points[i].time-ta>.250) break;
            const auto d=raw[j]-raw[j-1],v=c.path[i]-raw[j-1]; const double sq=d.x*d.x+d.y*d.y;
            if(sq<1e-10) continue;
            const double f=std::clamp((v.x*d.x+v.y*d.y)/sq,0.0,1.0);
            const double distance=length(c.path[i]-(raw[j-1]+d*f));
            if(distance<best-1e-9) { best=distance; bestTime=ta+(tb-ta)*f; }
        }
        lags.push_back((stroke.points[i].time-bestTime)*1000);
    }
    c.lagSamples=lags.size();
    for(double lag:lags) c.lagMeanMs+=lag;
    if(!lags.empty()) {
        c.lagMeanMs/=lags.size(); std::sort(lags.begin(),lags.end());
        c.lagP95Ms=lags[std::min(lags.size()-1,static_cast<std::size_t>(std::ceil(lags.size()*.95)-1))];
    }
    if(clean) for(std::size_t i=2;i+2<raw.size();++i) {
        const auto a=raw[i]-raw[i-2],b=raw[i+2]-raw[i]; const double la=length(a),lb=length(b);
        if(la>.1 && lb>.1 && (a.x*b.x+a.y*b.y)/(la*lb)<.5) {
            ++c.turnSamples; c.turnDisplacementMax=std::max(c.turnDisplacementMax,length(c.path[i]-raw[i]));
        }
    }
    const double area=polygonArea(raw);
    if(clean && raw.size()>2 && length(raw.front()-raw.back())<5 && std::abs(area)>1) {
        c.loopAreaAvailable=true; c.loopAreaRatio=polygonArea(c.path)/area;
    }
}
void validate(const Candidate& c) {
    if(c.algorithm<Algorithm::Raw || c.algorithm>Algorithm::OfflineGaussian) throw std::invalid_argument("Unknown candidate algorithm");
    for(double x:{c.radius,c.window,c.cap,c.cutoff,c.beta,c.derivative})
        if(!std::isfinite(x) || x<0) throw std::invalid_argument("Invalid candidate parameter");
    if(c.algorithm!=Algorithm::Raw && (c.cap<=0 || c.cap>50)) throw std::invalid_argument("Invalid candidate cap");
    if((c.algorithm==Algorithm::Local || c.algorithm==Algorithm::Buffered || c.algorithm==Algorithm::OfflineGaussian) &&
        (c.window<=0 || c.window>1)) throw std::invalid_argument("Invalid candidate window");
    if(c.algorithm==Algorithm::OneEuro && (c.cutoff<=0 || c.cutoff>1000 || c.derivative<=0 || c.derivative>1000 || c.beta>100))
        throw std::invalid_argument("Invalid One Euro settings");
}
}
Comparison compare(const Stroke& stroke,const Candidate& candidate,bool score) {
    validate(candidate); Comparison result; result.candidate=candidate;
    if(candidate.algorithm==Algorithm::OfflineGaussian && (!stroke.ended || stroke.canceled)) return result;
    result.available=true; const auto raw=rawPath(stroke); result.path=raw;
    const auto movement=motion(stroke,raw);
    if(candidate.algorithm==Algorithm::Local) result.path=localFilter(stroke,candidate.radius,candidate.cap,candidate.window);
    else if(candidate.algorithm!=Algorithm::Raw) for(std::size_t first=0;first<raw.size();) {
        std::size_t end=first+1; while(end<raw.size() && movement[end].valid()) ++end;
        Run run(stroke,first,end); Vec velocity{},filtered=raw[first];
        for(std::size_t i=first+1;i<end;++i) {
            const double t=stroke.points[i].time,dt=movement[i].dt;
            if(candidate.algorithm==Algorithm::OneEuro) {
                velocity=velocity+(movement[i].velocity-velocity)*alpha(candidate.derivative,dt);
                filtered=filtered+(raw[i]-filtered)*alpha(candidate.cutoff+candidate.beta*length(velocity),dt);
                filtered=capped(filtered,raw[i],candidate.cap); result.path[i]=filtered;
            } else if(candidate.algorithm==Algorithm::Buffered) {
                const double begin=std::max(run.times.front(),t-candidate.window);
                const auto integral=run.at(t).second-run.at(begin).second;
                if(t>begin) result.path[i]=capped(integral*(1/(t-begin)),raw[i],candidate.cap);
            } else if(candidate.algorithm==Algorithm::OfflineGaussian && i+1<end) {
                // Fixed 25-node quadrature in time, independent of report density.
                const double radius=std::min({candidate.window,t-run.times.front(),run.times.back()-t});
                if(radius<=0) continue;
                Vec sum{}; double weights=0;
                for(int j=-12;j<=12;++j) {
                    const double u=j/12.0,weight=std::exp(-4.5*u*u)*(std::abs(j)==12?.5:1);
                    sum=sum+run.at(t+u*radius).first*weight; weights+=weight;
                }
                const double edge=std::min({1.0,(t-run.times.front())/candidate.window,(run.times.back()-t)/candidate.window});
                result.path[i]=capped(raw[i]+(sum*(1/weights)-raw[i])*(edge*edge*(3-2*edge)),raw[i],candidate.cap);
            }
        }
        first=end;
    }
    if(score) { result.metrics=measure(stroke,result.path); scoreExtras(result,stroke,raw,movement); }
    return result;
}
std::array<Comparison,candidateCount> compareAll(const Stroke& s,LocalOptions o,bool score) {
    std::array<Comparison,candidateCount> result; const auto catalog=candidates(o);
    for(unsigned i=0;i<candidateCount;++i) result[i]=compare(s,catalog[i],score);
    return result;
}
void writeComparisonCsv(std::ostream& out,const Processor& processor,LocalOptions options,bool paths,bool sweep) {
    out.imbue(std::locale::classic()); out<<std::setprecision(17);
    const auto catalog=candidates(options);
    auto list=sweep?sweepCandidates():std::vector<Candidate>(catalog.begin(),catalog.end());
    if(sweep) list[0]=catalog[0]; // Keep the user's adjustable baseline in the sweep too.
    list.insert(list.begin(),{"raw","Reported reference",Algorithm::Raw});
    const char* common="version,stroke,candidate,execution,available,radius_dip,window_ms,cap_dip,min_cutoff_hz,beta,derivative_hz";
    out<<common;
    if(paths) out<<",sequence,time_seconds,clock_recovered,raw_x_dip,raw_y_dip,result_x_dip,result_y_dip\n";
    else out<<",points,raw_speed_dip_s,variation_5dip,variation_10dip,variation_20dip,variation_samples_5,variation_samples_10,variation_samples_20,displacement_rms_dip,displacement_max_dip,endpoint_dip,nearest_path_lag_mean_ms,nearest_path_lag_p95_ms,lag_samples,raw_turn_displacement_max_dip,raw_turn_samples,closed_loop_area_ratio,invalid_speed_intervals\n";
    unsigned index=0;
    for(const auto& stroke:processor.strokes()) {
        ++index; const auto rawMetrics=measure(stroke,rawPath(stroke));
        for(const auto& candidate:list) {
            const auto c=compare(stroke,candidate,!paths);
            const auto prefix=[&](){out<<"0.4.0,"<<index<<','<<candidate.id<<','<<executionKind(candidate.algorithm)<<','<<c.available<<','
                <<candidate.radius<<','<<candidate.window*1000<<','<<candidate.cap<<','<<candidate.cutoff<<','<<candidate.beta<<','<<candidate.derivative;};
            if(paths) {
                if(!c.available) { prefix(); out<<",,,,,,,\n"; }
                else for(std::size_t i=0;i<stroke.points.size();++i) {
                    const auto& s=stroke.points[i]; prefix(); out<<','<<s.sequence<<','<<s.time<<','<<s.timingRecovered<<','
                        <<s.p.x<<','<<s.p.y<<','<<c.path[i].x<<','<<c.path[i].y<<'\n';
                }
            } else {
                prefix(); out<<','<<stroke.points.size()<<',';
                if(rawMetrics.speedIntervals) out<<rawMetrics.meanSpeed;
                if(!c.available) { out<<",,,,,,,,,,,,,,,,\n"; continue; }
                for(const auto& v:c.variation) { out<<','; if(v.samples) out<<v.rms; }
                for(const auto& v:c.variation) out<<','<<v.samples;
                out<<','<<c.metrics.displacementRms<<','<<c.metrics.displacementMax<<','<<c.metrics.endpointDisplacement<<',';
                if(c.lagSamples) out<<c.lagMeanMs;
                out<<','; if(c.lagSamples) out<<c.lagP95Ms;
                out<<','<<c.lagSamples<<','; if(c.turnSamples) out<<c.turnDisplacementMax;
                out<<','<<c.turnSamples<<','; if(c.loopAreaAvailable) out<<c.loopAreaRatio;
                out<<','<<c.metrics.invalidSpeedIntervals<<'\n';
            }
        }
    }
    if(!out) throw std::runtime_error("Failed writing comparison export");
}
}
