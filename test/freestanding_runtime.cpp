// wasm32-unknown-unknown (freestanding) スモークテスト用の最小ランタイム。
// -nostdlib で必要になるごくわずかなシンボル（アロケータ / mem* / __cxa_*）を提供する。
// 実運用のゲストでは、埋め込み側のランタイムが同等のシンボルを提供すること。
#include <cstddef>
#include <cstdint>
#include <cstdlib>  // libc++ の _LIBCPP_ABI_NAMESPACE を取得するため

extern "C" unsigned char __heap_base[];  // wasm-ld が供給するデータ領域終端（＝ヒープ開始）

namespace {
unsigned char* heapPtr = nullptr;
}

extern "C" {

// 前進のみのバンプアロケータ（スモークテスト専用。free は無視する）
void* malloc(std::size_t n) {
  if (heapPtr == nullptr) {
    heapPtr = __heap_base;
  }
  auto      p       = reinterpret_cast<std::uintptr_t>(heapPtr);
  p                 = (p + 15u) & ~static_cast<std::uintptr_t>(15u);
  heapPtr           = reinterpret_cast<unsigned char*>(p + n);
  return reinterpret_cast<void*>(p);
}

void free(void*) {}

void* calloc(std::size_t n, std::size_t sz) {
  void* p = malloc(n * sz);
  auto* b = static_cast<unsigned char*>(p);
  for (std::size_t i = 0; i < n * sz; ++i) {
    b[i] = 0;
  }
  return p;
}

void* realloc(void*, std::size_t n) {
  return malloc(n);
}

// mem* 系（コンパイラが生成する libcalls）
void* memcpy(void* dst, const void* src, std::size_t n) {
  auto* d = static_cast<unsigned char*>(dst);
  auto* s = static_cast<const unsigned char*>(src);
  while (n-- > 0) {
    *d++ = *s++;
  }
  return dst;
}

void* memmove(void* dst, const void* src, std::size_t n) {
  auto* d = static_cast<unsigned char*>(dst);
  auto* s = static_cast<const unsigned char*>(src);
  if (d < s) {
    while (n-- > 0) {
      *d++ = *s++;
    }
  } else {
    d += n;
    s += n;
    while (n-- > 0) {
      *--d = *--s;
    }
  }
  return dst;
}

void* memset(void* dst, int c, std::size_t n) {
  auto* d = static_cast<unsigned char*>(dst);
  while (n-- > 0) {
    *d++ = static_cast<unsigned char>(c);
  }
  return dst;
}

int memcmp(const void* a, const void* b, std::size_t n) {
  auto* x = static_cast<const unsigned char*>(a);
  auto* y = static_cast<const unsigned char*>(b);
  for (; n > 0; --n, ++x, ++y) {
    if (*x != *y) {
      return *x < *y ? -1 : 1;
    }
  }
  return 0;
}

void* memchr(void* p, int c, std::size_t n) {
  auto* s = static_cast<unsigned char*>(p);
  for (; n > 0; --n, ++s) {
    if (*s == static_cast<unsigned char>(c)) {
      return s;
    }
  }
  return nullptr;
}

std::size_t strlen(const char* s) {
  std::size_t n = 0;
  while (s[n] != '\0') {
    ++n;
  }
  return n;
}

// Itanium C++ ABI が要求する最小限のシンボル
void abort() {
  __builtin_trap();
}
void __cxa_pure_virtual() {
  __builtin_trap();
}
int __cxa_atexit(void (*)(void*), void*, void*) {
  return 0;  // スモークテストでは静的デストラクタ不要
}
void __cxa_finalize(void*) {}
int  __cxa_guard_acquire(long long*) {
  return 1;
}
void __cxa_guard_release(long long*) {}
void __cxa_guard_abort(long long*) {}

}  // extern "C"

// C++ リンケージが必要な new / delete（コンテナの確保経路）
// libc++ の診断出力カスタマイズポイント（libc++.a の verbose_abort.o が stdio に依存するため自前定義する）
#include <cstdlib>  // _LIBCPP_ABI_NAMESPACE を取得するため

namespace std {
inline namespace _LIBCPP_ABI_NAMESPACE {
void __libcpp_verbose_abort(const char*, ...) {
  __builtin_trap();
}
}  // namespace _LIBCPP_ABI_NAMESPACE
}  // namespace std

void* operator new(std::size_t n) {
  void* p = malloc(n);
  if (p == nullptr) {
    __builtin_trap();
  }
  return p;
}
void* operator new[](std::size_t n) {
  return ::operator new(n);
}
void operator delete(void*) noexcept {}
void operator delete[](void*) noexcept {}
void operator delete(void*, std::size_t) noexcept {}
void operator delete[](void*, std::size_t) noexcept {}
