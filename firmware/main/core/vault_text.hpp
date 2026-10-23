#pragma once
#include "password_vault.hpp"
namespace vault {
inline const char *categoryLabel(const std::string &category) {
  return category == "work" ? "工作" : category == "app" ? "应用" : category == "web" ? "网页" : "其他";
}
inline const char *targetLabel(const std::string &category) {
  return category == "work" ? "单位 / 系统" : category == "app" ? "应用 / 平台" : "用途";
}
// UTF-8 plain text, shared by on-device QR and authenticated browser copy.
inline std::string plainText(const Entry &entry) {
  std::string text = "名称：" + entry.title + "\n分类：" + categoryLabel(entry.category);
  if (entry.category != "other") text += "\n账号：" + entry.username;
  text += entry.category == "other" ? "\n码值：" : "\n密码：";
  text += entry.password;
  if (!entry.target.empty()) text += std::string("\n") + targetLabel(entry.category) + "：" + entry.target;
  if (!entry.note.empty()) text += "\n备注：" + entry.note;
  return text;
}
}
