#include <gtkmm.h>
#include <cairomm/context.h>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <iomanip>

struct CoreData {
    double cpu_percent = 0.0;
    double target_cpu_percent = 0.0;
    unsigned long long prev_total = 0;
    unsigned long long prev_idle  = 0;

    double clock_fill = 0.0;
    double target_clock_fill = 0.0;

    unsigned long long max_khz = 0;
    unsigned long long cur_khz = 0;
};

class AppWindow : public Gtk::Window {
public:
    AppWindow() {
        set_title("CPU Meter");

        num_threads = std::thread::hardware_concurrency();
        parse_cpuinfo();
        load_max_frequencies();

        set_default_size(1018, 1199);

        set_child(drawing_area);
        drawing_area.set_draw_func(sigc::mem_fun(*this, &AppWindow::on_draw));

        Glib::signal_timeout().connect(sigc::mem_fun(*this, &AppWindow::on_timeout), 16);
        Glib::signal_timeout().connect(sigc::mem_fun(*this, &AppWindow::update_cpu_usage), 100);
        Glib::signal_timeout().connect(sigc::mem_fun(*this, &AppWindow::update_temperature), 1000);
        Glib::signal_timeout().connect(sigc::mem_fun(*this, &AppWindow::update_clock_freq), 200);
    }

private:
    Gtk::DrawingArea drawing_area;
    std::vector<CoreData> cores;
    unsigned int num_threads = 0;
    unsigned int num_cores = 0;

    double package_temperature = 35.0;

    bool on_timeout() {
        for (auto &core : cores) {
            core.cpu_percent += (core.target_cpu_percent - core.cpu_percent) * 0.1;
            core.clock_fill  += (core.target_clock_fill - core.clock_fill) * 0.15;
        }
        drawing_area.queue_draw();
        return true;
    }

    void parse_cpuinfo() {
        std::ifstream file("/proc/cpuinfo");
        std::string line;

        while (std::getline(file, line)) {
            if (line.rfind("processor", 0) == 0)
                cores.emplace_back();
        }

        num_cores = cores.size();
    }

    bool update_cpu_usage() {
        std::ifstream file("/proc/stat");
        std::string line;
        size_t core_idx = 0;

        while (std::getline(file, line)) {

            if (line.rfind("cpu",0) != 0) continue;
            if (core_idx >= cores.size()) break;

            std::istringstream ss(line);
            std::string cpu;
            unsigned long long user,nice,system,idle,iowait,irq,softirq,steal;

            ss>>cpu>>user>>nice>>system>>idle>>iowait>>irq>>softirq>>steal;

            unsigned long long idle_time=idle+iowait;
            unsigned long long total_time=user+nice+system+idle+iowait+irq+softirq+steal;

            unsigned long long delta_total=total_time-cores[core_idx].prev_total;
            unsigned long long delta_idle=idle_time-cores[core_idx].prev_idle;

            if(delta_total>0)
                cores[core_idx].target_cpu_percent=(double)(delta_total-delta_idle)/delta_total;

            cores[core_idx].prev_total=total_time;
            cores[core_idx].prev_idle=idle_time;

            core_idx++;
        }

        return true;
    }

    bool update_temperature() {

        std::ifstream temp_file("/sys/class/thermal/thermal_zone0/temp");

        if(temp_file){
            double t;
            temp_file>>t;
            package_temperature=t/1000.0;
        }else{
            double avg=0;
            for(auto &c:cores) avg+=c.cpu_percent;
            avg/=cores.size();
            package_temperature=35.0+avg*55.0;
        }

        return true;
    }

    static bool read_ull_from_file(const std::string& path,unsigned long long& out){
        std::ifstream f(path);
        if(!f.is_open()) return false;
        f>>out;
        return !f.fail();
    }

    void load_max_frequencies(){

        for(size_t i=0;i<cores.size();i++){

            unsigned long long max_khz=0;

            std::string p1="/sys/devices/system/cpu/cpu"+std::to_string(i)+"/cpufreq/scaling_max_freq";

            if(read_ull_from_file(p1,max_khz)){
                cores[i].max_khz=max_khz;
                continue;
            }

            std::string p2="/sys/devices/system/cpu/cpu"+std::to_string(i)+"/cpufreq/cpuinfo_max_freq";

            if(read_ull_from_file(p2,max_khz)){
                cores[i].max_khz=max_khz;
                continue;
            }

            cores[i].max_khz=0;
        }
    }

    bool update_clock_freq(){

        for(size_t i=0;i<cores.size();i++){

            if(cores[i].max_khz==0){
                cores[i].target_clock_fill=0;
                cores[i].cur_khz=0;
                continue;
            }

            unsigned long long cur_khz=0;

            std::string p1="/sys/devices/system/cpu/cpu"+std::to_string(i)+"/cpufreq/scaling_cur_freq";

            if(!read_ull_from_file(p1,cur_khz)){

                std::string p2="/sys/devices/system/cpu/cpu"+std::to_string(i)+"/cpufreq/cpuinfo_cur_freq";

                if(!read_ull_from_file(p2,cur_khz)){
                    cores[i].target_clock_fill=0;
                    cores[i].cur_khz=0;
                    continue;
                }
            }

            cores[i].cur_khz=cur_khz;

            double fill=(double)cur_khz/cores[i].max_khz;

            if(fill<0) fill=0;
            if(fill>1) fill=1;

            cores[i].target_clock_fill=fill;
        }

        return true;
    }

    void draw_core(const Cairo::RefPtr<Cairo::Context>& cr,double x,double y,double radius,const CoreData& core){

        double start_angle=-M_PI/2.0;
        double end_angle=start_angle+2*M_PI*core.cpu_percent;

        cr->begin_new_path();
        cr->set_source_rgb(0.2,0.2,0.2);
        cr->arc(x,y,radius+5,0,2*M_PI);
        cr->fill();

        double r,g,b;
        double p=core.cpu_percent;

        if(p<0.3){r=0;g=0.5+p;b=0;}
        else if(p<0.6){double t=(p-0.3)/0.3;r=t;g=1;b=0;}
        else if(p<0.8){double t=(p-0.6)/0.2;r=1;g=1-t*0.5;b=0;}
        else{double t=(p-0.8)/0.2;r=1;g=0.5-t*0.5;b=0;}

        for(int i=0;i<6;i++){
            double lw=10+4*i;
            double alpha=0.06-0.01*i;

            cr->begin_new_path();
            cr->set_line_width(lw);
            cr->set_source_rgba(r,g,b,alpha);
            cr->set_line_cap(Cairo::Context::LineCap::ROUND);
            cr->arc(x,y,radius,start_angle,end_angle);
            cr->stroke();
        }

        cr->begin_new_path();
        cr->set_line_width(10);
        cr->set_source_rgb(r,g,b);
        cr->arc(x,y,radius,start_angle,end_angle);
        cr->stroke();

        cr->set_source_rgb(1,1,1);
        cr->set_font_size(radius/2.0);

        std::stringstream ss;
        ss<<int(core.cpu_percent*100)<<"%";

        Cairo::TextExtents te;

        cr->get_text_extents(ss.str(),te);

        cr->move_to(x-te.width/2-te.x_bearing,y-te.height/2-te.y_bearing);
        cr->show_text(ss.str());

        // -------- CLOCK BAR LAYOUT --------

        cr->begin_new_path();

        const std::string clock_label="clock:";

        cr->set_source_rgb(1,1,1);
        cr->set_font_size(12);

        Cairo::TextExtents te2;
        cr->get_text_extents(clock_label,te2);

        double baseline_y=y+radius+16;

        const double bar_w=radius*1.4;
        const double bar_h=6;

        double bar_x=x-(bar_w/2);
        double bar_y=baseline_y-bar_h;

        const double gap=8;

        double label_x=bar_x-gap-te2.width;

        cr->move_to(label_x,baseline_y);
        cr->show_text(clock_label);

        cr->begin_new_path();

        cr->set_source_rgb(0.25,0.05,0.05);
        cr->rectangle(bar_x,bar_y,bar_w,bar_h);
        cr->fill();

        double fill=core.clock_fill;

        if(fill<0) fill=0;
        if(fill>1) fill=1;

        const double barR=0.9,barG=0,barB=0;

        cr->set_source_rgb(barR,barG,barB);
        cr->rectangle(bar_x,bar_y,bar_w*fill,bar_h);
        cr->fill();

        int pct=(int)round(fill*100);

        std::string pct_text=std::to_string(pct)+"%";

        const double pct_gap=8;

        double pct_x=bar_x+bar_w+pct_gap;

        cr->set_source_rgb(barR,barG,barB);
        cr->set_font_size(12);

        cr->move_to(pct_x,baseline_y);
        cr->show_text(pct_text);

        double ghz=(double)core.cur_khz/1000000.0;

        std::stringstream ghz_ss;
        ghz_ss<<std::fixed<<std::setprecision(1)<<ghz<<" GHz";

        std::string ghz_text="  "+ghz_ss.str();

        Cairo::TextExtents pct_ext;
        cr->get_text_extents(pct_text,pct_ext);

        cr->set_source_rgb(0,1,0);

        cr->move_to(pct_x+pct_ext.width,baseline_y);
        cr->show_text(ghz_text);

        cr->begin_new_path();
    }

    void draw_temperature_bar(const Cairo::RefPtr<Cairo::Context>& cr,double x,double y_top,double y_bottom){

        double bar_width=50;
        double bar_height=y_bottom-y_top;

        double t_bottom=35,t_top=90;

        double t_pct=(package_temperature-t_bottom)/(t_top-t_bottom);

        if(t_pct>1)t_pct=1;
        if(t_pct<0)t_pct=0;

        cr->set_source_rgb(0.3,0.3,0.3);
        cr->rectangle(x,y_top,bar_width,bar_height);
        cr->stroke();

        cr->set_source_rgb(1,0,0);
        cr->rectangle(x,y_top+bar_height*(1-t_pct),bar_width,bar_height*t_pct);
        cr->fill();

        cr->set_source_rgb(1,1,1);
        cr->set_font_size(16);

        std::stringstream ss;
        ss<<std::fixed<<std::setprecision(1)<<package_temperature<<"°C";

        Cairo::TextExtents te;
        cr->get_text_extents(ss.str(),te);

        cr->move_to(x+(bar_width-te.width)/2-te.x_bearing,y_top-10);
        cr->show_text(ss.str());
    }

    void on_draw(const Cairo::RefPtr<Cairo::Context>& cr,int width,int height){

        cr->set_source_rgb(0.08,0.08,0.08);
        cr->paint();

        std::string header=
        std::to_string(num_cores)+" cores detected, "+
        std::to_string(num_threads)+" threads available";

        cr->set_source_rgb(1,1,1);
        cr->set_font_size(20);

        Cairo::TextExtents ext;
        cr->get_text_extents(header,ext);

        cr->move_to((width-ext.width)/2-ext.x_bearing,25);
        cr->show_text(header);

        int cols=std::min(4,(int)cores.size());
        int rows=ceil((double)cores.size()/cols);

        double left_margin=20;
        double top_margin=50;
        double bottom_margin=40;

        double temp_bar_width=70;
        double right_margin=temp_bar_width+20;

        double grid_width=width-left_margin-right_margin;
        double grid_height=height-top_margin-bottom_margin;

        double padding_x=grid_width/cols;
        double padding_y=grid_height/rows;

        double grid_top=top_margin+padding_y/2;
        double grid_bottom=top_margin+padding_y*rows;

        for(size_t i=0;i<cores.size();i++){

            int row=i/cols;
            int col=i%cols;

            double x=left_margin+padding_x*(col+0.5);
            double y=top_margin+padding_y*(row+0.5);

            draw_core(cr,x,y,std::min(padding_x,padding_y)/4,cores[i]);
        }

        double temp_x=width-right_margin+10;

        draw_temperature_bar(cr,temp_x,grid_top,grid_bottom);

        cr->set_source_rgb(1,1,1);
        cr->set_font_size(14);

        std::stringstream ss;
        ss<<"Window: "<<width<<" x "<<height;

        cr->move_to(10,height-10);
        cr->show_text(ss.str());
    }
};

class CpuMeterApp : public Gtk::Application {

protected:

    CpuMeterApp():Gtk::Application("com.example.cpumeter"){}

    void on_activate() override{

        auto window=new AppWindow();

        add_window(*window);

        window->present();
    }

public:

    static Glib::RefPtr<CpuMeterApp> create(){

        return Glib::make_refptr_for_instance<CpuMeterApp>(new CpuMeterApp());
    }
};

int main(int argc,char* argv[]){

    auto app=CpuMeterApp::create();

    return app->run(argc,argv);
}