// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('duplex_settings_test', function() {
  suite('DuplexSettingsTest', function() {
    /** @type {?PrintPreviewDuplexSettingsElement} */
    let duplexSection = null;

    /** @type {?PrintPreviewModelElement} */
    let model = null;

    /** @override */
    setup(function() {
      PolymerTest.clearBody();
      model = document.createElement('print-preview-model');
      document.body.appendChild(model);
      model.set('settings.duplex.available', true);
      model.set('settings.duplex.value', false);
      model.set('settings.duplexShortEdge.available', true);

      duplexSection = document.createElement('print-preview-duplex-settings');
      duplexSection.settings = model.settings;
      duplexSection.disabled = false;
      test_util.fakeDataBind(model, duplexSection, 'settings');
      document.body.appendChild(duplexSection);
      Polymer.dom.flush();
    });

    // Tests that making short edge unavailable prevents the collapse from
    // showing.
    test('short edge unavailable', function() {
      const collapse = duplexSection.$$('iron-collapse');
      duplexSection.setSetting('duplex', true);
      assertTrue(collapse.opened);

      [false, true].forEach(value => {
        model.set('settings.duplexShortEdge.available', value);
        assertEquals(value, collapse.opened);
      });
    });

    // Tests that setting the setting updates the UI.
    test('set setting', async () => {
      const checkbox = duplexSection.$$('cr-checkbox');
      const collapse = duplexSection.$$('iron-collapse');
      assertFalse(checkbox.checked);
      assertFalse(collapse.opened);

      duplexSection.setSetting('duplex', true);
      assertTrue(checkbox.checked);
      assertTrue(collapse.opened);

      const select = duplexSection.$$('select');
      assertEquals(
          print_preview_new.DuplexMode.LONG_EDGE.toString(), select.value);

      duplexSection.setSetting('duplexShortEdge', true);
      await test_util.eventToPromise('process-select-change', duplexSection);
      assertEquals(
          print_preview_new.DuplexMode.SHORT_EDGE.toString(), select.value);
    });

    // Tests that checking the box or selecting a new option in the dropdown
    // updates the setting.
    test('select option', async () => {
      const checkbox = duplexSection.$$('cr-checkbox');
      const collapse = duplexSection.$$('iron-collapse');
      assertFalse(checkbox.checked);
      assertFalse(collapse.opened);
      assertFalse(duplexSection.getSettingValue('duplex'));
      assertFalse(duplexSection.getSettingValue('duplexShortEdge'));

      checkbox.checked = true;
      checkbox.dispatchEvent(new CustomEvent('change'));
      assertTrue(collapse.opened);
      assertTrue(duplexSection.getSettingValue('duplex'));
      assertFalse(duplexSection.getSettingValue('duplexShortEdge'));

      const select = duplexSection.$$('select');
      assertEquals(
          print_preview_new.DuplexMode.LONG_EDGE.toString(), select.value);
      assertEquals(2, select.options.length);

      // Verify that selecting an new option in the dropdown sets the setting.
      await print_preview_test_utils.selectOption(
          duplexSection, print_preview_new.DuplexMode.SHORT_EDGE.toString());
      assertTrue(duplexSection.getSettingValue('duplex'));
      assertTrue(duplexSection.getSettingValue('duplexShortEdge'));
    });

    if (cr.isChromeOS) {
      // Tests that if settings are enforced by enterprise policy the
      // appropriate UI is disabled.
      test('disabled by policy', function() {
        const checkbox = duplexSection.$$('cr-checkbox');
        assertFalse(checkbox.disabled);

        duplexSection.setSetting('duplex', true);
        const select = duplexSection.$$('select');
        assertFalse(select.disabled);

        model.set('settings.duplex.setByPolicy', true);
        assertTrue(checkbox.disabled);
        assertFalse(select.disabled);

        model.set('settings.duplexShortEdge.setByPolicy', true);
        assertTrue(checkbox.disabled);
        assertTrue(select.disabled);
      });
    }
  });
});
