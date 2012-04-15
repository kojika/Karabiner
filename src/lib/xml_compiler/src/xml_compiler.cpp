#include <exception>
#include <iostream>
#include <sstream>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include "pqrs/string.hpp"
#include "pqrs/xml_compiler.hpp"

namespace pqrs {
  void
  xml_compiler::reload(void)
  {
    error_information_.clear();
    replacement_.clear();
    symbol_map_.clear();
    app_vector_.clear();
    preferences_checkbox_node_tree_.clear();
    preferences_number_node_tree_.clear();
    identifier_map_.clear();
    remapclasses_initialize_vector_.clear();
    simultaneous_keycode_index_ = 0;

    try {
      // ------------------------------------------------------------
      // Loading replacement
      {
        replacement_loader loader(*this, replacement_);

        // private.xml
        {
          ptree_ptr ptree_ptr;
          pqrs::string::replacement dummy; // Use dummy replacement when we read <replacementdef>.
          read_xml_(ptree_ptr,
                    xml_file_path(xml_file_path::base_directory::private_xml, "private.xml"),
                    dummy);
          if (ptree_ptr) {
            loader.traverse(*ptree_ptr);
          }
        }
        // replacementdef.xml
        {
          ptree_ptr ptree_ptr;
          pqrs::string::replacement dummy; // Use dummy replacement when we read <replacementdef>.
          read_xml_(ptree_ptr,
                    xml_file_path(xml_file_path::base_directory::system_xml,  "replacementdef.xml"),
                    dummy);
          if (ptree_ptr) {
            loader.traverse(*ptree_ptr);
          }
        }
      }

      // ------------------------------------------------------------
      // Then, we read private.xml with replacement and loaders share it.

      ptree_ptr private_xml_ptree_ptr;
      read_xml_(private_xml_ptree_ptr,
                xml_file_path(xml_file_path::base_directory::private_xml, "private.xml"));
      if (private_xml_ptree_ptr && private_xml_ptree_ptr->empty()) {
        private_xml_ptree_ptr.reset();
      }

      // symbol_map
      {
        symbol_map_loader loader(*this, symbol_map_);

        if (private_xml_ptree_ptr) {
          loader.traverse(*private_xml_ptree_ptr);
        }

        loader_wrapper<symbol_map_loader>::traverse_system_xml(*this, loader, "symbol_map.xml");
      }

      // app
      {
        app_loader loader(*this, symbol_map_, app_vector_);

        if (private_xml_ptree_ptr) {
          loader.traverse(*private_xml_ptree_ptr);
        }

        loader_wrapper<app_loader>::traverse_system_xml(*this, loader, "appdef.xml");
      }

      // device
      {
        device_loader loader(*this, symbol_map_);

        if (private_xml_ptree_ptr) {
          loader.traverse(*private_xml_ptree_ptr);
        }

        loader_wrapper<device_loader>::traverse_system_xml(*this, loader, "devicevendordef.xml");
        loader_wrapper<device_loader>::traverse_system_xml(*this, loader, "deviceproductdef.xml");
      }

      // config_index, remapclasses_initialize_vector, preferences_node
      {
        ptree_ptr checkbox_xml_ptree_ptr;
        read_xml_(checkbox_xml_ptree_ptr,
                  xml_file_path(xml_file_path::base_directory::system_xml, "checkbox.xml"));

        ptree_ptr number_xml_ptree_ptr;
        read_xml_(number_xml_ptree_ptr,
                  xml_file_path(xml_file_path::base_directory::system_xml, "number.xml"));

        // config_index
        {}
        // remapclasses_initialize_vector
        {
          // ----------------------------------------
          // add_config_index_and_keycode_to_symbol_map_
          //   1st loop: <identifier>notsave.*</identifier>
          //   2nd loop: other <identifier>
          //
          // We need to assign higher priority to notsave.* settings.
          // So, adding config_index by 2steps.
          if (private_xml_ptree_ptr) {
            add_config_index_and_keycode_to_symbol_map_(*private_xml_ptree_ptr, "", true);
          }
          if (checkbox_xml_ptree_ptr) {
            add_config_index_and_keycode_to_symbol_map_(*checkbox_xml_ptree_ptr, "", true);
          }

          if (private_xml_ptree_ptr) {
            add_config_index_and_keycode_to_symbol_map_(*private_xml_ptree_ptr, "", false);
          }
          if (checkbox_xml_ptree_ptr) {
            add_config_index_and_keycode_to_symbol_map_(*checkbox_xml_ptree_ptr, "", false);
          }

          // ----------------------------------------
          if (private_xml_ptree_ptr) {
            traverse_identifier_(*private_xml_ptree_ptr, "");
          }
          if (checkbox_xml_ptree_ptr) {
            traverse_identifier_(*checkbox_xml_ptree_ptr, "");
          }

          remapclasses_initialize_vector_.freeze();
        }
        // preferences_node
        {
          preferences_node_loader loader(*this,
                                         preferences_checkbox_node_tree_,
                                         preferences_number_node_tree_);

          if (private_xml_ptree_ptr) {
            loader.traverse_checkbox(*private_xml_ptree_ptr);
          }

          if (checkbox_xml_ptree_ptr) {
            loader.traverse_checkbox(*checkbox_xml_ptree_ptr);
          }

          if (number_xml_ptree_ptr) {
            loader.traverse_number(*number_xml_ptree_ptr);
          }
        }
      }

    } catch (std::exception& e) {
      error_information_.set(e.what());
    }
  }

  void
  xml_compiler::read_xml_(ptree_ptr& out,
                          const std::string& base_diretory,
                          const std::string& relative_file_path,
                          const pqrs::string::replacement& replacement) const
  {
    try {
      out.reset(new boost::property_tree::ptree());

      std::string path = base_diretory + "/" + relative_file_path;

      std::string xml;
      if (replacement.empty()) {
        pqrs::string::string_from_file(xml, path.c_str());
      } else {
        pqrs::string::string_by_replacing_double_curly_braces_from_file(xml, path.c_str(), replacement);
      }
      std::stringstream istream(xml, std::stringstream::in);

      int flags = boost::property_tree::xml_parser::no_comments;

      boost::property_tree::read_xml(istream, *out, flags);

    } catch (std::exception& e) {
      std::string what = e.what();

      // Hack:
      // boost::property_tree::read_xml throw exception with filename.
      // But, when we call read_xml with stream, the filename becomes "unspecified file" as follow.
      //
      // <unspecified file>(4): expected element name
      //
      // So, we change "unspecified file" to file name by ourself.
      boost::replace_first(what, "<unspecified file>", std::string("<") + relative_file_path + ">");

      error_information_.set(what);
    }
  }

  void
  xml_compiler::read_xml_(ptree_ptr& out,
                          const xml_file_path& xml_file_path,
                          const pqrs::string::replacement& replacement) const
  {
    switch (xml_file_path.get_base_directory()) {
      case xml_file_path::base_directory::system_xml:
        read_xml_(out,
                  system_xml_directory_,
                  xml_file_path.get_relative_path(),
                  replacement);
        break;

      case xml_file_path::base_directory::private_xml:
        read_xml_(out,
                  private_xml_directory_,
                  xml_file_path.get_relative_path(),
                  replacement);
        break;
    }
  }

  void
  xml_compiler::read_xmls_(std::vector<ptree_ptr>& pt_ptrs,
                           const std::vector<xml_file_path_ptr>& xml_file_path_ptrs) const
  {
    pt_ptrs.clear();

    for (auto& path_ptr : xml_file_path_ptrs) {
      ptree_ptr pt_ptr;
      read_xml_(pt_ptr, *path_ptr);
      if (pt_ptr) {
        pt_ptrs.push_back(pt_ptr);
      }
    }
  }

  boost::optional<const std::string&>
  xml_compiler::get_identifier(int config_index) const
  {
    auto it = identifier_map_.find(config_index);
    if (it == identifier_map_.end()) {
      return boost::none;
    }
    return it->second;
  }

  uint32_t
  xml_compiler::get_appid(const std::string& application_identifier) const
  {
    for (auto& it : app_vector_) {
      if (! it) continue;

      if (it->is_rules_matched(application_identifier)) {
        auto name = it->get_name();
        if (! name) goto notfound;

        auto v = symbol_map_.get_optional(std::string("ApplicationType::") + *name);
        if (! v) goto notfound;

        return *v;
      }
    }

  notfound:
    // return ApplicationType::UNKNOWN (== 0)
    return 0;
  }

  void
  xml_compiler::normalize_identifier(std::string& identifier)
  {
    boost::replace_all(identifier, ".", "_");
  }

  void
  xml_compiler::extract_include_(ptree_ptr& out,
                                 const boost::property_tree::ptree::value_type& it) const
  {
    out.reset();

    if (it.first != "include") return;

    out.reset(new boost::property_tree::ptree());

    // ----------------------------------------
    // replacement
    pqrs::string::replacement r;
    if (! it.second.empty()) {
      replacement_loader loader(*this, r);
      loader.traverse(it.second);
    }

    for (auto& i : replacement_) {
      if (r.find(i.first) == r.end()) {
        r[i.first] = i.second;
      }
    }

    // ----------------------------------------
    {
      auto path = it.second.get_optional<std::string>("<xmlattr>.path");
      if (path) {
        read_xml_(out, private_xml_directory_, *path, r);
        return;
      }
    }
    {
      auto path = it.second.get_optional<std::string>("<xmlattr>.system_xml_path");
      if (path) {
        read_xml_(out, system_xml_directory_, *path, r);
        return;
      }
    }
  }
}