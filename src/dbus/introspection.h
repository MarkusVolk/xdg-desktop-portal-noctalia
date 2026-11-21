#pragma once

// Introspection data of the backend interfaces, from xdg-desktop-portal's data/.

namespace xdpn::introspection {

  inline constexpr char kSettings[] = R"xml(<node>
<interface name="org.freedesktop.impl.portal.Settings">
    <method name="ReadAll">
      <arg type="as" name="namespaces" direction="in"/>
      <annotation name="org.qtproject.QtDBus.QtTypeName.Out0" value="QMap&lt;QString,QVariantMap&gt;"/>
      <arg type="a{sa{sv}}" name="value" direction="out"/>
    </method>
    <method name="Read">
      <arg type="s" name="namespace" direction="in"/>
      <arg type="s" name="key" direction="in"/>
      <arg type="v" name="value" direction="out"/>
    </method>
    <signal name="SettingChanged">
      <arg type="s" name="namespace" direction="out"/>
      <arg type="s" name="key" direction="out"/>
      <arg type="v" name="value" direction="out"/>
    </signal>
    <property name="version" type="u" access="read"/>
  </interface>
</node>)xml";

  inline constexpr char kAccess[] = R"xml(<node>
<interface name="org.freedesktop.impl.portal.Access">
    <method name="AccessDialog">
      <arg type="o" name="handle" direction="in"/>
      <arg type="s" name="app_id" direction="in"/>
      <arg type="s" name="parent_window" direction="in"/>
      <arg type="s" name="title" direction="in"/>
      <arg type="s" name="subtitle" direction="in"/>
      <arg type="s" name="body" direction="in"/>
      <annotation name="org.qtproject.QtDBus.QtTypeName.In6" value="QVariantMap"/>
      <arg type="a{sv}" name="options" direction="in"/>
      <arg type="u" name="response" direction="out"/>
      <annotation name="org.qtproject.QtDBus.QtTypeName.Out1" value="QVariantMap"/>
      <arg type="a{sv}" name="results" direction="out"/>
    </method>
  </interface>
</node>)xml";

  inline constexpr char kAppChooser[] = R"xml(<node>
<interface name="org.freedesktop.impl.portal.AppChooser">
    <method name="ChooseApplication">
      <arg type="o" name="handle" direction="in"/>
      <arg type="s" name="app_id" direction="in"/>
      <arg type="s" name="parent_window" direction="in"/>
      <arg type="as" name="choices" direction="in"/>
      <annotation name="org.qtproject.QtDBus.QtTypeName.In4" value="QVariantMap"/>
      <arg type="a{sv}" name="options" direction="in"/>
      <arg type="u" name="response" direction="out"/>
      <annotation name="org.qtproject.QtDBus.QtTypeName.Out1" value="QVariantMap"/>
      <arg type="a{sv}" name="results" direction="out"/>
    </method>
    <method name="UpdateChoices">
      <arg type="o" name="handle" direction="in"/>
      <arg type="as" name="choices" direction="in"/>
    </method>
  </interface>
</node>)xml";

  inline constexpr char kInhibit[] = R"xml(<node>
<interface name="org.freedesktop.impl.portal.Inhibit">
    <method name="Inhibit">
      <arg type="o" name="handle" direction="in"/>
      <arg type="s" name="app_id" direction="in"/>
      <arg type="s" name="window" direction="in"/>
      <arg type="u" name="flags" direction="in"/>
      <annotation name="org.qtproject.QtDBus.QtTypeName.In4" value="QVariantMap"/>
      <arg type="a{sv}" name="options" direction="in"/>
    </method>
    <method name="CreateMonitor">
      <arg type="o" name="handle" direction="in"/>
      <arg type="o" name="session_handle" direction="in"/>
      <arg type="s" name="app_id" direction="in"/>
      <arg type="s" name="window" direction="in"/>
      <arg type="u" name="response" direction="out"/>
    </method>
    <signal name="StateChanged">
      <arg type="o" name="session_handle" direction="out"/>
      <annotation name="org.qtproject.QtDBus.QtTypeName.Out1" value="QVariantMap"/>
      <arg type="a{sv}" name="state" direction="out"/>
    </signal>
    <method name="QueryEndResponse">
      <arg type="o" name="session_handle" direction="in"/>
    </method>
  </interface>
</node>)xml";

  inline constexpr char kRequest[] = R"xml(<node>
<interface name="org.freedesktop.impl.portal.Request">
    <method name="Close">
    </method>
  </interface>
</node>)xml";

  inline constexpr char kSession[] = R"xml(<node>
<interface name="org.freedesktop.impl.portal.Session">
    <method name="Close">
    </method>
    <signal name="Closed">
    </signal>
    <property name="version" type="u" access="read"/>
  </interface>
</node>)xml";

} // namespace xdpn::introspection
