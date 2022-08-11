#pragma once

#include "./randomgen.hpp"
#include <bits/stdc++.h>
#include <ext/pb_ds/assoc_container.hpp>
#include <ext/pb_ds/hash_policy.hpp>

using namespace std;
namespace CCHash {
__gnu_pbds::cc_hash_table<std::string, ll> hashTable;

inline void init(ll size, ll len) {
  initRng();
  getStringTable(size, len);
}

inline void addRandomString() { ++hashTable[randomString()]; }

inline void getRandomString() { --hashTable[randomString()]; }

} // namespace GPHash