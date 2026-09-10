#include "aegis/simulation.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
int main(int argc, char** argv)
{
    try
    {
        aegis::Scenario s;
        std::string output, trace;
        for (int i = 1; i < argc; i += 2)
        {
            if (i + 1 >= argc)
                throw std::runtime_error("every option requires a value");
            const std::string key = argv[i], value = argv[i + 1];
            auto integer = [&]()
            {
                std::size_t end = 0;
                auto n = std::stoull(value, &end);
                if (end != value.size() || value[0] == '-')
                    throw std::runtime_error("invalid integer");
                return n;
            };
            if (key == "--seed")
            {
                auto n = integer();
                if (n > UINT32_MAX)
                    throw std::runtime_error("seed out of range");
                s.seed = static_cast<std::uint32_t>(n);
            }
            else if (key == "--enemies")
            {
                auto n = integer();
                if (n > 50)
                    throw std::runtime_error("enemy count out of range");
                s.enemies = static_cast<int>(n);
            }
            else if (key == "--duration")
            {
                auto n = integer();
                if (n > 300)
                    throw std::runtime_error("duration out of range");
                s.duration = static_cast<double>(n);
            }
            else if (key == "--policy")
            {
                if (value != "utility" && value != "priority")
                    throw std::runtime_error("unknown policy");
                s.utilityCompanion = value == "utility";
            }
            else if (key == "--arena")
                s.arena = value;
            else if (key == "--director")
            {
                if (value != "on" && value != "off")
                    throw std::runtime_error("director expects on/off");
                s.director = value == "on";
            }
            else if (key == "--output")
                output = value;
            else if (key == "--trace")
                trace = value;
            else
                throw std::runtime_error("unknown option: " + key);
        }
        if (!s.valid())
            throw std::runtime_error("invalid scenario");
        aegis::Simulation sim(s, trace);
        const auto metrics = sim.run();
        if (output.empty())
            aegis::writeJson(std::cout, s, metrics);
        else
        {
            std::ofstream file(output);
            if (!file)
                throw std::runtime_error("cannot open result file");
            aegis::writeJson(file, s, metrics);
        }
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "aegis_sim: " << e.what() << '\n';
        return 2;
    }
}
