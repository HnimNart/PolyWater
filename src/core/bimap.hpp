#pragma once

#include <optional>
#include <unordered_map>

namespace core
{

template <typename TLeft, typename TRight> class Bimap
{
private:
  std::unordered_map<TLeft, TRight> leftToRight;
  std::unordered_map<TRight, TLeft> rightToLeft;

public:
  /**********************************************************/
  void insert(const TLeft& left, const TRight& right)
  /**********************************************************/
  {
    // Remove existing entries to maintain 1:1 mapping integrity
    removeByLeft(left);
    removeByRight(right);

    leftToRight[left] = right;
    rightToLeft[right] = left;
  }

  // Generic Getters
  std::optional<TRight> getRight(const TLeft& left) const
  {
    if (auto it = leftToRight.find(left); it != leftToRight.end())
      return it->second;
    return std::nullopt;
  }

  std::optional<TLeft> getLeft(const TRight& right) const
  {
    if (auto it = rightToLeft.find(right); it != rightToLeft.end())
      return it->second;
    return std::nullopt;
  }

  // Removal Logic
  void removeByLeft(const TLeft& left)
  {
    if (auto it = leftToRight.find(left); it != leftToRight.end())
    {
      rightToLeft.erase(it->second);
      leftToRight.erase(it);
    }
  }

  void removeByRight(const TRight& right)
  {
    if (auto it = rightToLeft.find(right); it != rightToLeft.end())
    {
      leftToRight.erase(it->second);
      rightToLeft.erase(it);
    }
  }

  void clear() noexcept
  {
    leftToRight.clear();
    rightToLeft.clear();
  }

  size_t size() const noexcept { return leftToRight.size(); }
};

}  // namespace core
