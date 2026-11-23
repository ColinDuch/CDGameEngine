#pragma once 

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

/*
chronomètre des phases d'un chargement, chaque mark() note le temps écoulé 
depuis le précédent. Il dira ce qui ralentit l'ouverture d'un projet
et servira de base à la barre de chargement.
*/

class LoadTimer {
    public:
        LoadTimer() : start(Clock::now()), last(start) {}
        
        // termine la phase en cours et lui donne son nom
        void mark(const std::string& phase) {
            const auto now = Clock::now();
            phases.push_back({ phase, ms(last, now) });
            last = now;
        }

        void report() const{
            double longest = 0.0;
            for (const auto& p : phases) longest = std::max(longest, p.second);

            std::printf("[load] temps de chargement :\n");
            for (const auto& [name, t] : phases) {
                std::printf("         %-28s %8.1f ms%s\n", name.c_str(), t,
                        (t == longest && t > 0.0) ? "   <-- le plus long" : "");
            }
            std::printf("         %-28s %8.1f ms\n", "TOTAL", ms(start, last));
        }

    private:
        using Clock = std::chrono::steady_clock;
        static double ms(Clock::time_point a, Clock::time_point b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        }

        Clock::time_point start, last;
        std::vector<std::pair<std::string, double>> phases;
};