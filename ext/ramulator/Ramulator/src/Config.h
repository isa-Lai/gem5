#ifndef __CONFIG_H
#define __CONFIG_H

#include <string>
#include <fstream>
#include <vector>
#include <map>
#include <iostream>
#include <cassert>

namespace ramulator
{

class Config {

private:
    int channels;
    int ranks;
    int subarrays;
    int cpu_tick;
    int mem_tick;
    int core_num = 0;
    long expected_limit_insts = 0;
    long warmup_insts = 0;
    bool print_cmd_trace_flag = false;
    bool print_ipp_cmd_trace_flag = false;
    bool memory_trace_flag = false;
    
    std::map<std::string, std::string> options;
    std::map<std::string, std::string> sim_options;
    std::vector<std::string> trace_files;
    bool use_sim_options = false;
    std::string tracefile_directory;

    std::map<std::string, std::string> defaults = {
        // DRAM
        {"standard", "LPDDR4"},
        {"speed", "LPDDR4_3200"},
        {"org", "LPDDR4_8Gb_x16"},
        {"channels", "1"},
        {"ranks", "1"},
        {"subarrays", "128"},
        // Memory Controller
        {"row_policy", "opened"},
        {"timeout_row_policy_threshold", "120"},
        {"disable_refresh", "false"},
        // CPU
        {"cores", "1"},
        {"cpu_tick", "5"},
        {"mem_tick", "2"},
        {"early_exit", "off"},
        {"expected_limit_insts", "200000000"},
        {"warmup_insts", "100000000"},
        {"translation", "Random"},
        // Cache
        {"cache", "L3"},
        {"l3_size", "4194304"},
        {"prefetcher", "off"}, // "off" or "stride
        // Stride Prefetcher
        {"stride_pref_entries", "1024"},
        {"stride_pref_mode", "1"}, // 0 -> single stride mode, 1 -> multi stride mode
        {"stride_pref_single_stride_tresh", "6"},
        {"stride_pref_multi_stride_tresh", "6"},
        {"stride_pref_stride_start_dist", "1"},
        {"stride_pref_stride_degree", "4"},
        {"stride_pref_stride_dist", "16"},
        // Other
        {"record_cmd_trace", "off"},
        {"print_cmd_trace", "off"},
        {"collect_row_activation_histogram", "off"}
    };




    template<typename T>
    T get(const std::string& param_name, T (*cast_func)(const std::string&)) const {

        std::string param = this->operator[](param_name);

        if(param == "") {
            param = defaults.at(param_name); // get the default param, if exists

            if(param == "") {
                std::cerr << "ERROR: All options should have their default values in Config.h!" << std::endl;
                std::cerr << "No default value found for: " << param_name << std::endl;
                exit(-1);
            }
        }

        try {
            return (*cast_func)(param); 
        } catch (const std::invalid_argument& ia) {
            std::cerr << "Invalid argument: " << ia.what() << std::endl;
            exit(-1);
        } catch (const std::out_of_range& oor) {
            std::cerr << "Out of Range error: " << oor.what() << std::endl;
            exit(-1);
        } catch (...) {
            std::cerr << "Error! Unhandled exception." << std::endl;
            std::exit(-1);
        }

        return T();
    }

    static bool param_to_bool(const std::string& s) {
        if(s == "true")
            return true;

        if(s == "on")
            return true;

        return false;
    }

     bool sim_contains(const std::string& name) const {
        if(sim_options.find(name) != sim_options.end()) {
            return true;
        } else {
            return false;
        }
    }    

public:
    Config() {}
    Config(const std::string& fname);
    void parse(const std::string& fname);
    void parse_cmdline(const int argc, char** argv);

    std::string operator [] (const std::string& name) const {
       if(use_sim_options && sim_contains(name))
           return (sim_options.find(name))->second;

       if (contains(name)) {
         return (options.find(name))->second;
       } else {
         return "";
       }
    }

    int get_int(const std::string& param_name) const {
        
        return get<int>(param_name, [](const std::string& s){ return std::stoi(s); }); // Hasan: the lambda function trick helps ignoring the optional argument of stoi
    }
    
    long get_long(const std::string& param_name) const {
        
        return get<long>(param_name, [](const std::string& s){ return std::stol(s); });
    }
    
    float get_float(const std::string& param_name) const {

        return get<float>(param_name, [](const std::string& s){ return std::stof(s); });
    }

    bool get_bool(const std::string& param_name) const {
        return get<bool>(param_name, param_to_bool);
    }

    std::string get_str(const std::string& param_name) const {
        return get<std::string>(param_name, [](const std::string& s){ return s; });
    }


    bool contains(const std::string& name) const {

      if(use_sim_options && sim_contains(name))
          return true;

      if (options.find(name) != options.end()) {
        return true;
      } else {
        return false;
      }
    } 

    void add (const std::string& name, const std::string& value) {

      if(use_sim_options) {
        if(!sim_contains(name))
            sim_options.insert(make_pair(name, value));
        else
            printf("ramulator::Config::add options[%s] already set.\n", name.c_str());

        return;
      }

      if (!contains(name)) {
        options.insert(make_pair(name, value));
      } else {
        printf("ramulator::Config::add options[%s] already set.\n", name.c_str());
      }
    }

    template<typename T>
    void update (const std::string& name, const T& value) {
        if(use_sim_options)
            sim_options[name] = std::to_string(value);
        else
            options[name] = std::to_string(value);
    }

    void enable_sim_options () {
        use_sim_options = true;
    }

    void disable_sim_options () {
        use_sim_options = false;
    }

    bool has_l3_cache() const {
      const std::string& cache_type = get_str("cache");   
      return (cache_type == "all") || (cache_type == "L3");
    }

    bool has_core_caches() const {
      const std::string& cache_type = get_str("cache");   
      return (cache_type == "all" || cache_type == "L1L2");
    }
    

    bool calc_weighted_speedup() const {
      return (get_long("expected_limit_insts") != 0);
    }

    const std::vector<std::string>& get_trace_files() const {
        return trace_files;
    }

    std::string get_tracefile_directory() const { return tracefile_directory; }
    void set_tracefile_directory(std::string directory) { tracefile_directory = directory; }
    

    void set_core_num(int _core_num) {core_num = _core_num;}

    int get_channels() const {return channels;}
    int get_subarrays() const {return subarrays;}
    int get_ranks() const {return ranks;}
    int get_cpu_tick() const {return cpu_tick;}
    int get_mem_tick() const {return mem_tick;}
    int get_core_num() const {return core_num;}
    long get_expected_limit_insts() const {return expected_limit_insts;}
    long get_warmup_insts() const {return warmup_insts;}
    
    

    bool is_early_exit() const {
      // the default value is true
      if (options.find("early_exit") != options.end()) {
        if ((options.find("early_exit"))->second == "off") {
          return false;
        }
        return true;
      }
      return true;
    }


    bool record_cmd_trace() const {
      // the default value is false
      if (options.find("record_cmd_trace") != options.end()) {
        if ((options.find("record_cmd_trace"))->second == "on") {
          return true;
        }
        return false;
      }
      return false;
    }

    // remove old bool print_cmd_trace() const
    /* 
    bool print_cmd_trace() const {
    */
    bool print_memory_trace() const {
      return this->memory_trace_flag;
    }
    
};


} /* namespace ramulator */

#endif /* _CONFIG_H */

