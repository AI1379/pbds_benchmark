#pragma once

#include "./randomgen.hpp"
#include <bits/stdc++.h>

using namespace std;
namespace STLMap {
std::map<std::string, ll> hashTable;

inline void init(ll size, ll len) {
  initRng();
  getStringTable(size, len);
}

inline void addRandomString() { ++hashTable[randomString()]; }

inline void getRandomString() { --hashTable[randomString()]; }

} // namespace GPHash