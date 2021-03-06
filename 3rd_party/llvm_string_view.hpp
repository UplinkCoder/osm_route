#include <cassert>
#include <cstring>
#include <string>

#pragma once

class StringView {
   const char *First;
   const char *Last;

 public:
   static const size_t npos = ~size_t(0);

   template <size_t N>
   StringView(const char (&Str)[N]) : First(Str), Last(Str + N - 1) {}

   StringView(const char *First_, const char *Last_)
       : First(First_), Last(Last_) {}

   StringView(const char *First_, size_t Len)
       : First(First_), Last(First_ + Len) {}

   StringView(const char *Str) : First(Str), Last(Str + std::strlen(Str)) {}

   StringView(const std::string& Str) : First(Str.data()), Last(Str.data() + Str.size()) {}

   StringView() : First(nullptr), Last(nullptr) {}

   StringView substr(size_t Pos, size_t Len = npos) const {
     assert(Pos <= size());
     if (Len > size() - Pos)
       Len = size() - Pos;
     return StringView(begin() + Pos, Len);
   }

   size_t find(char C, size_t From = 0) const {
     // Avoid calling memchr with nullptr.
     if (From < size()) {
       // Just forward to memchr, which is faster than a hand-rolled loop.
       if (const void *P = ::memchr(First + From, C, size() - From))
         return size_t(static_cast<const char *>(P) - First);
     }
     return npos;
   }

   StringView dropFront(size_t N = 1) const {
     if (N >= size())
       N = size();
     return StringView(First + N, Last);
   }

   StringView dropBack(size_t N = 1) const {
     if (N >= size())
       N = size();
     return StringView(First, Last - N);
   }

   char front() const {
     assert(!empty());
     return *begin();
   }

   char back() const {
     assert(!empty());
     return *(end() - 1);
   }

   char popFront() {
     assert(!empty());
     return *First++;
   }

   bool consumeFront(char C) {
     if (!startsWith(C))
       return false;
     *this = dropFront(1);
     return true;
   }

   bool consumeFront(StringView S) {
     if (!startsWith(S))
       return false;
     *this = dropFront(S.size());
     return true;
   }

   bool startsWith(char C) const { return !empty() && *begin() == C; }

   bool startsWith(StringView Str) const {
     if (Str.size() > size())
       return false;
     return std::strncmp(Str.begin(), begin(), Str.size()) == 0;
   }

   const char &operator[](size_t Idx) const { return *(begin() + Idx); }

   const char *data() const { return First; }

   const char *begin() const { return First; }
   const char *end() const { return Last; }
   size_t size() const { return static_cast<size_t>(Last - First); }
   bool empty() const { return First == Last; }
 };

 inline bool operator==(const StringView &LHS, const StringView &RHS) {
   return LHS.size() == RHS.size() &&
          std::strncmp(LHS.begin(), RHS.begin(), LHS.size()) == 0;
 }
