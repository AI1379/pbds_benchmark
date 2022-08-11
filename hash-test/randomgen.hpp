#pragma once

#include "corecrt.h"
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <utility>

#define ll long long
#define pll pair<ll, ll>

const ll MAXN = 1000010;
const ll kSeed = 1145141919810;
const char kTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::shared_ptr<std::mt19937_64> rng_p = nullptr;
std::vector<std::string> stringTable;

inline void initRng() { rng_p = std::make_shared<std::mt19937_64>(kSeed); }
inline std::mt19937_64 &rng() {
  if (rng_p == nullptr)
    initRng();
  return *rng_p;
}

inline std::string randomString(ll len) {
  std::uniform_int_distribution<size_t> posd(0, 63);
  std::stringstream ss;
  while (len--)
    ss << posd(rng());
  return ss.str();
}

inline void getStringTable(ll size, ll len) {
  stringTable.resize(size);
  while (size--) {
    stringTable.emplace_back(randomString(len));
  }
}

inline std::string randomString() {
  std::uniform_int_distribution<size_t> posd(0, stringTable.size() - 1);
  return stringTable[posd(rng())];
}
