//          Copyright Boston University SESA Group 2013 - 2014.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#include "Trans.h"

#include <atomic>

#include "Cpu.h"
#include "CpuAsm.h"
#include "Debug.h"
#include "EarlyPageAllocator.h"
#include "PageAllocator.h"
#include "VMem.h"

void ebbrt::trans::Init() {
  auto page = early_page_allocator::AllocatePage(1, Cpu::GetMyNode());
  vmem::TraversePageTable(
      vmem::GetPageTableRoot(), kVMemStart, kVMemStart + pmem::kPageSize, 0, 4,
      [=](vmem::Pte& entry, uint64_t base_virt, size_t level) {
        kassert(!entry.Present());
        entry.Set(page.ToAddr() + (base_virt - kVMemStart), level > 0);
        std::atomic_thread_fence(std::memory_order_release);
        asm volatile("invlpg (%[addr])" : : [addr] "r"(base_virt) : "memory");
      },
      [](vmem::Pte& entry) {
        auto page = early_page_allocator::AllocatePage(1, Cpu::GetMyNode());
        auto page_addr = page.ToAddr();
        new (reinterpret_cast<void*>(page_addr)) vmem::Pte[512];
        entry.SetNormal(page_addr);
        return true;
      });
  std::memset(reinterpret_cast<void*>(page.ToAddr()), 0, pmem::kPageSize);
}

void ebbrt::trans::ApInit(size_t index) {
  auto pte_root = vmem::Pte(ReadCr3());
  auto idx = vmem::PtIndex(kVMemStart, 3);
  auto pt = reinterpret_cast<vmem::Pte*>(pte_root.Addr(false));
  pt[idx].Clear();

  auto nid = Cpu::GetByIndex(index)->nid();
  auto& p_allocator = (*PageAllocator::allocators)[nid.val()];

  vmem::TraversePageTable(
      pte_root, kVMemStart, kVMemStart + pmem::kPageSize, 0, 4,
      [&](vmem::Pte& entry, uint64_t base_virt, size_t level) {
        kassert(!entry.Present());
        auto page = p_allocator.Alloc(0, nid);
        std::memset(reinterpret_cast<void*>(page.ToAddr()), 0, pmem::kPageSize);
        entry.Set(page.ToAddr() + (base_virt - kVMemStart), level > 0);
        std::atomic_thread_fence(std::memory_order_release);
        asm volatile("invlpg (%[addr])" : : [addr] "r"(base_virt) : "memory");
      },
      [&](vmem::Pte& entry) {
        auto page = p_allocator.Alloc(0, nid);
        auto page_addr = page.ToAddr();
        new (reinterpret_cast<void*>(page_addr)) vmem::Pte[512];
        entry.SetNormal(page_addr);
        return true;
      });
}

void ebbrt::trans::HandleFault(idt::ExceptionFrame* ef, uintptr_t fault_addr) {
  auto vpage = ebbrt::Pfn::Down(fault_addr);
  auto backing_page = ebbrt::page_allocator->Alloc();
  memset(reinterpret_cast<void*>(backing_page.ToAddr()), 0, pmem::kPageSize);
  kbugon(backing_page == ebbrt::Pfn::None(),
         "Failed to allocate page for translation system\n");
  ebbrt::vmem::MapMemory(vpage, backing_page);
}
