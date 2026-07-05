#include "Config.h"
#include <unistd.h>
#include <sstream>
#include <string>
#include <getopt.h>
using namespace std;
using namespace ramulator;

Config::Config(const std::string& fname) {
  parse(fname);
}

void Config::parse(const string& fname)
{
    ifstream file(fname);
    assert(file.good() && "Bad config file");
    string line;
    while (getline(file, line)) {
        char delim[] = " \t=";
        vector<string> tokens;

        while (true) {
            size_t start = line.find_first_not_of(delim);
            if (start == string::npos) 
                break;

            size_t end = line.find_first_of(delim, start);
            if (end == string::npos) {
                tokens.push_back(line.substr(start));
                break;
            }

            tokens.push_back(line.substr(start, end - start));
            line = line.substr(end);
        }

        // empty line
        if (!tokens.size())
            continue;

        // comment line
        if (tokens[0][0] == '#')
            continue;
        /*printf("Token 1 = %s , token2 = %s\n", tokens[0].c_str(),tokens[1].c_str());*/
        // parameter line
        assert(tokens.size() == 2 && "Only allow two tokens in one line");

        options[tokens[0]] = tokens[1];

        if(tokens[0] == "print_cmd_trace")
        {  if(tokens[1]=="on")
            print_cmd_trace_flag = true;
        }
        else if(tokens[0] == "memory_trace")
        {  if(tokens[1]=="on")
            memory_trace_flag = true;
        }
        else if(tokens[0] == "print_ipp_cmd_trace")
        {  if(tokens[1]=="on")
            print_ipp_cmd_trace_flag  = true;
        }
         
        
        printf("Ramulator Config [%s]] = %s\n",tokens[0].c_str(),tokens[1].c_str());


        if (tokens[0] == "channels") {
          channels = atoi(tokens[1].c_str());
        } else if (tokens[0] == "ranks") {
          ranks = atoi(tokens[1].c_str());
        } else if (tokens[0] == "subarrays") {
          subarrays = atoi(tokens[1].c_str());
        } else if (tokens[0] == "cpu_tick") {
          cpu_tick = atoi(tokens[1].c_str());
        } else if (tokens[0] == "mem_tick") {
          mem_tick = atoi(tokens[1].c_str());
        } else if (tokens[0] == "expected_limit_insts") {
          expected_limit_insts = atoi(tokens[1].c_str());
        } else if (tokens[0] == "warmup_insts") {
          warmup_insts = atoi(tokens[1].c_str());
        }
        
    }
    file.close();
}


static struct option long_options[] = {
            {"mode",     required_argument, 0,  'm' },
            {"stats",  required_argument, 0,  's' },
            {"param",  required_argument, 0,  'p' },
            {"sim-param",  required_argument, 0,  'c' }, // use to specify parameters that will only be applied after warmup
            {"trace",  required_argument, 0,  't' },
            {0,         0,                 0,  0 }
        };

void Config::parse_cmdline(const int argc, char** argv) {

    int option;
    while ((option = getopt_long (argc, argv, "s:m:p:t:c:", long_options, NULL)) != -1) {
        switch(option) {
            case 'c':
            case 'p':{
                std::stringstream ss(optarg);
                std::string item;
                std::vector<std::string> elems;
                while (std::getline(ss, item, '=')) {
                    elems.push_back(std::move(item));
                }
                
                assert(elems.size() == 2 && "Invalid command line argument");

                std::string name = elems[0];
                std::string value = elems[1];

                if(option == 'c') {
                    sim_options[name] = value;
                    break;
                }

                options[name] = value;

                break; }
            case 't':{
                trace_files.push_back(optarg);

                break; }
            case 'm':{
                if(contains("mode"))
                    options["mode"] = optarg;
                else
                    add("mode", optarg);
                break; }
            case 's': {
                if(contains("stats"))
                    options["stats"] = optarg;
                else
                    add("stats", optarg);
                break;}
			case ':':   /* missing option argument */
        		std::cerr << "Error! " << argv[0] << ": option `-" << (char)optopt << "' requires an argument" << std::endl; 
				exit(-1);
        		break;
            case '?':
            default:
        		std::cerr << "Warning! " << argv[0] << ": option `-" << (char)optopt << "' is invalid. Ignored." << std::endl;
				break; 
        }
    }


}


