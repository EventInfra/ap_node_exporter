include $(TOPDIR)/rules.mk

PKG_NAME:=ap-node-exporter
PKG_VERSION:=0.0.1
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(BUILD_DIR)/ap-node-exporter-$(PKG_VERSION)
PKG_SOURCE:=ap-node-exporter-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=@SF/ap-node-exporter
PKG_MD5SUM:=9b7dc52656f5cbec846a7ba3299f73bd
PKG_CAT:=zcat

include $(INCLUDE_DIR)/package.mk

define Package/ap-node-exporter
  SECTION:=base
  CATEGORY:=Network
  TITLE:=Prometheus AP node exporter
  #DESCRIPTION:=This variable is obsolete. use the Package/name/description define instead!
  URL:=http://bridge.sourceforge.net/
endef

define Package/bridge/description
 AP Node exporter for prometheus - collects metrics from the WiFi stack
endef

define Build/Configure
  $(call Build/Configure/Default,--with-linux-headers=$(LINUX_DIR))
endef

define Package/ap-node-exporter/install
        $(INSTALL_DIR) $(1)/usr/sbin
        $(INSTALL_BIN) $(PKG_BUILD_DIR)/node_exp $(1)/usr/sbin/
endef

$(eval $(call BuildPackage,ap-node-exporter))
