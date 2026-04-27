#pragma once

#include <optional>
#include <unordered_map>

<<<<<<< HEAD
namespace core
{

template <typename TLeft, typename TRight> class Bimap
{
=======
namespace core {

template <typename TLeft, typename TRight> class Bimap {
>>>>>>> macos
private:
  std::unordered_map<TLeft, TRight> leftToRight;
  std::unordered_map<TRight, TLeft> rightToLeft;

public:
<<<<<<< HEAD
  void insert(const TLeft& left, const TRight& right)
  {
=======
  void insert(const TLeft &left, const TRight &right) {
>>>>>>> macos
    // Remove existing entries to maintain 1:1 mapping integrity
    removeByLeft(left);
    removeByRight(right);

    leftToRight[left] = right;
    rightToLeft[right] = left;
  }

  // Generic Getters
<<<<<<< HEAD
  std::optional<TRight> getRight(const TLeft& left) const
  {
=======
  std::optional<TRight> getRight(const TLeft &left) const {
>>>>>>> macos
    if (auto it = leftToRight.find(left); it != leftToRight.end())
      return it->second;
    return std::nullopt;
  }

<<<<<<< HEAD
  std::optional<TLeft> getLeft(const TRight& right) const
  {
=======
  std::optional<TLeft> getLeft(const TRight &right) const {
>>>>>>> macos
    if (auto it = rightToLeft.find(right); it != rightToLeft.end())
      return it->second;
    return std::nullopt;
  }

  // Removal Logic
<<<<<<< HEAD
  void removeByLeft(const TLeft& left)
  {
    if (auto it = leftToRight.find(left); it != leftToRight.end())
    {
=======
  void removeByLeft(const TLeft &left) {
    if (auto it = leftToRight.find(left); it != leftToRight.end()) {
>>>>>>> macos
      rightToLeft.erase(it->second);
      leftToRight.erase(it);
    }
  }

<<<<<<< HEAD
  void removeByRight(const TRight& right)
  {
    if (auto it = rightToLeft.find(right); it != rightToLeft.end())
    {
=======
  void removeByRight(const TRight &right) {
    if (auto it = rightToLeft.find(right); it != rightToLeft.end()) {
>>>>>>> macos
      leftToRight.erase(it->second);
      rightToLeft.erase(it);
    }
  }

<<<<<<< HEAD
  void clear() noexcept
  {
=======
  void clear() noexcept {
>>>>>>> macos
    leftToRight.clear();
    rightToLeft.clear();
  }

  size_t size() const noexcept { return leftToRight.size(); }
};

<<<<<<< HEAD
}  // namespace core
=======
} // namespace core
>>>>>>> macos
