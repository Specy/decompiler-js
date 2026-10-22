// A FactCheck-like program: builds a flag in a std::string with += and
// comparisons, plus the C++ features whose names the research needs.
#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace game {
struct Player {
    std::string name;
    int score = 0;
    explicit Player(std::string n) : name(std::move(n)) {}
    virtual ~Player() = default;
    virtual int bonus() const { return 1; }
};
struct Boss : Player {
    using Player::Player;
    int bonus() const override { return 5; }
};
template <typename T> T clampTo(T v, T lo, T hi) { return std::max(lo, std::min(v, hi)); }
int operator+(const Player &a, const Player &b) { return a.score + b.score; }
}  // namespace game

static std::string &greeting() {
    static std::string text = "hello";
    return text;
}

__attribute__((noinline)) int sum(const std::vector<int> &values) {
    int total = 0;
    for (int v : values) total += v;
    return total;
}
__attribute__((noinline)) int sum(const std::vector<std::string> &values) { return (int)values.size(); }

int main(int argc, char **argv) {
    std::string flag = "picoCTF{wELF_d0N3_mate_";
    std::string a = "1", b = "c", c = "4", d = "9";
    if (a[0] < 'B') flag += c;
    if (b[0] != 'A') flag += d;
    if (c[0] == 'G') flag += a;
    else flag += b;
    flag += '}';
    std::vector<std::string> parts;
    parts.push_back(flag);
    std::map<std::string, int> scores;
    scores[flag] = argc;
    auto add = [&](int x) { return x + argc; };
    std::vector<int> values{3, 1, 2};
    std::sort(values.begin(), values.end(), [](int l, int r) { return l > r; });
    auto boss = std::make_unique<game::Boss>("boss");
    game::Player player("p");
    try {
        if (argc > 5) throw std::runtime_error("too many");
    } catch (const std::exception &error) {
        std::cerr << error.what() << std::endl;
    }
    std::cout << greeting() << " " << add(game::clampTo(values[0], 0, 10)) << boss->bonus() << (player + *boss)
              << sum(values) << sum(parts) << scores.size() << std::endl;
    return 0;
}
