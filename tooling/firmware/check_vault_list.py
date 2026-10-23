"""List integration guards; geometry/mapping execute in vault_list_test.cpp."""
from pathlib import Path
import unittest
from check_vault_interaction import compact, section

ROOT = (Path(__file__).resolve().parents[2]/'firmware')


def prior_list_ui(text):
    # Normalize only the reviewed list changes for the older whole-section
    # hashes. PIN, detail, Bluetooth and all other UI bytes stay protected.
    text = text.replace('  resetListIndex();\n', '').replace('  clearListWidgets();\n', '')
    text = text.replace('m_page == Page::List ? listTitle()',
                        'm_page == Page::List ? categoryNames[m_category]')
    text = text.replace('''  if (m_page == Page::List && m_category == 2 && !webAccounts()) {
    m_webAccount = vault::interaction::WebIndex::None;
    selectList();
    m_message.clear();
    m_redraw = true;
    return true;
  }
''', '')
    text = text.replace('      const size_t count = m_categoryCounts[k];', '''      size_t count = 0;
      for (const auto &entry : m_session->entries)
        if (entry.category == categoryKeys[k])
          ++count;''')
    text = text.replace('''    if (m_listIndex.count())
      drawList();
    else''', '''    size_t count = 0;
    for (size_t i = 0; i < m_session->entries.size(); ++i) {
      const auto &entry = m_session->entries[i];
      if (entry.category != categoryKeys[m_category])
        continue;
      ++count;
      auto row = button(m_body, "", Action::Detail, i);
      lv_obj_set_height(row, 42);
      auto title = c::label(row, entry.title.c_str(), 229);
      lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 0);
      auto account = c::label(row, entry.username.c_str(), 229);
      lv_obj_set_style_text_font(account, &lv_font_montserrat_12, 0);
      lv_obj_set_style_text_color(account, ui::theme::muted(), 0);
      lv_obj_align(account, LV_ALIGN_BOTTOM_LEFT, 4, -1);
    }
    if (!count)''')
    return text


class VaultListContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = compact((ROOT/'main/apps/password_vault.cpp').read_text(encoding='utf-8'))

    def test_scroll_path_only_rebinds_existing_rows(self):
        update = section(self.app, 'voidPasswordVault::updateListRows()', 'voidPasswordVault::styleCategory(')
        for token in ('m_listRows[W::slot(i)]', 'm_listIndex.entry(i)',
                      'if(row.entry==index)continue;', 'entry.title.c_str()',
                      'entry.username.c_str()', 'int(i)*listPitch()'):
            self.assertIn(token, update)
        for token in ('lv_obj_clean(', 'lv_obj_create(', 'button(', 'c::label(',
                      'vault::start(', 'entry.note'):
            self.assertNotIn(token, update)

    def test_fixed_content_extent_and_bounded_touch_rows(self):
        draw = section(self.app, 'voidPasswordVault::drawList()', 'voidPasswordVault::draw()')
        for token in ('c::panel(m_listViewport,240,m_listIndex.height(listPitch()))',
                      'i<m_listIndex.slots()', 'lv_obj_set_height(row.button,W::RowHeight)',
                      'lv_obj_set_scroll_on_focus', 'LV_EVENT_SCROLL_BEGIN', 'LV_EVENT_SCROLL'):
            self.assertIn(token, draw)
        self.assertNotIn('m_session->entries.size()', draw)

    def test_return_restores_per_category_position(self):
        select = section(self.app, 'caseAction::Category:', 'caseAction::Address:')
        for token in ('m_category=index;m_webAccount=vault::interaction::WebIndex::None;',
                      'm_targetGroup=vault::interaction::TargetIndex::None;',
                      'm_targetIndex.build(m_session->entries,categoryKeys[m_category])',
                      'selectList();m_page=Page::List;'):
            self.assertIn(token, select)
        self.assertNotIn('m_targetOffsets.fill', select)
        build = section(self.app, 'voidPasswordVault::selectList()', 'voidPasswordVault::clearListWidgets()')
        self.assertIn('category==categoryKeys[m_category]', build)
        self.assertNotIn('m_listOffsets', build)
        draw = section(self.app, 'voidPasswordVault::drawList()', 'voidPasswordVault::draw()')
        self.assertIn('m_listIndex.clamp(listOffset()', draw)
        self.assertIn('lv_obj_scroll_to_y(m_listViewport,offset,LV_ANIM_OFF)', draw)

    def test_drag_cannot_activate_a_recycled_entry(self):
        click = section(self.app, 'voidPasswordVault::clicked(', 'voidPasswordVault::action(')
        self.assertIn('pressed!=(packed&0xffff)', click)
        self.assertIn('self->m_redraw||self->m_page!=Page::List', click)
        scroll = section(self.app, 'voidPasswordVault::listScrolled(', 'voidPasswordVault::updateListRows()')
        self.assertIn('m_pressedListEntry=vault::interaction::ListWindow::None', scroll)
        self.assertIn('lv_event_get_target_obj(e)!=self->m_listViewport', scroll)
        self.assertIn('self->m_redraw', scroll)
        self.assertIn('Action::TargetGroup', click)

    def test_session_replacement_resets_counts_and_offsets(self):
        reset = section(self.app, 'voidPasswordVault::resetListIndex()', 'voidPasswordVault::selectList()')
        for token in ('m_listIndex.clear()', 'm_categoryCounts.fill(0)',
                      'm_listOffsets.fill(0)', '++m_categoryCounts[k]'):
            self.assertIn(token, reset)
        self.assertEqual(self.app.count('m_session=std::move(m_job->output);resetListIndex();'), 2)

    def test_lock_and_redraw_detach_callbacks_before_deleting_widgets(self):
        lock = section(self.app, 'voidPasswordVault::lock()', 'voidPasswordVault::onHide()')
        self.assertLess(lock.index('clearListWidgets();'), lock.index('lv_obj_clean(root())'))
        draw = section(self.app, 'voidPasswordVault::draw()', 'casePage::Locked:')
        self.assertLess(draw.index('clearListWidgets();'), draw.index('lv_obj_clean(root())'))
        cleanup = section(self.app, 'voidPasswordVault::clearListWidgets()', 'voidPasswordVault::listPressed(')
        self.assertIn('m_listViewport=nullptr;', cleanup)
        self.assertIn('m_listRows.fill(ListRow{});', cleanup)

    def test_web_hierarchy_keeps_real_entry_ids_and_both_list_offsets(self):
        build = section(self.app, 'voidPasswordVault::selectList()', 'voidPasswordVault::clearListWidgets()')
        for token in ('m_listIndex.append(m_webIndex.entry(g))',
                      'm_listIndex.append(m_webIndex.entry(m_webAccount,i))'):
            self.assertIn(token, build)
        self.assertIn('m_webOffsets[m_webAccount]:m_listOffsets[m_category]', self.app)
        back = section(self.app, 'boolPasswordVault::onBack()', 'vault::StartResultPasswordVault::launch(')
        self.assertIn('m_page==Page::List&&m_category==2&&!webAccounts()', back)
        self.assertIn('m_webAccount=vault::interaction::WebIndex::None;selectList()', back)
        self.assertIn('m_webOffsets.fill(0)', self.app)
        self.assertIn('m_webIndex.clear()', self.app)
        self.assertIn('m_webIndex.build(m_session->entries)', self.app)

    def test_account_rows_cannot_skip_the_purpose_level(self):
        action = section(self.app, 'caseAction::Account:', 'caseAction::Detail:')
        self.assertIn('m_webAccount=m_webIndex.groupFor(index);selectList()', action)
        self.assertNotIn('Page::Detail', action)
        self.assertIn('webAccounts()?Action::Account:Action::Detail', self.app)

    def test_work_app_hierarchy_keeps_real_entry_ids_and_group_offsets(self):
        build = section(self.app, 'voidPasswordVault::selectList()', 'voidPasswordVault::clearListWidgets()')
        for token in ('if(targetCategory())', 'if(targetGroups())',
                      'm_listIndex.append(m_targetIndex.entry(g))',
                      'm_listIndex.append(m_targetIndex.entry(m_targetGroup,i))'):
            self.assertIn(token, build)
        select = section(self.app, 'caseAction::TargetGroup:', 'caseAction::Account:')
        for token in ('m_page==Page::List&&targetGroups()&&m_session',
                      'index<m_session->entries.size()',
                      'm_targetIndex.groupFor(index)!=vault::interaction::TargetIndex::None',
                      'm_targetGroup=m_targetIndex.groupFor(index);selectList()',
                      'm_access.leavePage()', 'm_reveal.hide()'):
            self.assertIn(token, select)
        self.assertNotIn('Page::Detail', select)
        offset = section(self.app, 'int32_t&PasswordVault::listOffset()', 'voidPasswordVault::selectList()')
        self.assertIn('m_targetGroup<m_targetIndex.groups()', offset)
        self.assertIn('m_targetOffsets[m_targetIndex.entry(m_targetGroup)]', offset)
        back = section(self.app, 'boolPasswordVault::onBack()', 'vault::StartResultPasswordVault::launch(')
        self.assertIn('m_page==Page::List&&targetCategory()&&!targetGroups()', back)
        self.assertIn('m_targetGroup=vault::interaction::TargetIndex::None;selectList()', back)
        self.assertLess(back.index('m_page==Page::Detail||'), back.index('targetCategory()'))

    def test_target_rows_use_existing_field_count_and_compact_touch_layout(self):
        helpers = section(self.app, 'boolPasswordVault::targetCategory()', 'int32_t&PasswordVault::listOffset()')
        for token in ('m_category==0||m_category==1', '"单位/系统"', '"应用名称/平台"',
                      'm_session->entries[index].target.empty()', 'return"未填写"',
                      'targetName(m_targetIndex.entry(m_targetGroup))'):
            self.assertIn(token, helpers)
        update = section(self.app, 'voidPasswordVault::updateListRows()', 'voidPasswordVault::styleCategory(')
        for token in ('targetGroups()?Action::TargetGroup', 'setText(row.title,targetName(index))',
                      'm_targetIndex.count(m_targetIndex.groupFor(index))', 'setText(row.account,count)'):
            self.assertIn(token, update)
        draw = section(self.app, 'voidPasswordVault::drawList()', 'voidPasswordVault::draw()')
        for token in ('if(webAccounts()||targetGroups())', 'lv_obj_set_size(row.title,172,21)',
                      'lv_obj_align(row.account,LV_ALIGN_RIGHT_MID,-4,0)'):
            self.assertIn(token, draw)

    def test_target_entries_cannot_cross_groups_and_reset_with_session(self):
        detail = section(self.app, 'caseAction::Detail:', 'caseAction::User:')
        for token in ('m_page==Page::List', '!webAccounts()&&!purposePage()&&!targetGroups()',
                      'm_session->entries[index].category==categoryKeys[m_category]',
                      'm_targetIndex.groupFor(index)==m_targetGroup'):
            self.assertIn(token, detail)
        reset = section(self.app, 'voidPasswordVault::resetListIndex()', 'boolPasswordVault::webAccounts()')
        for token in ('m_targetOffsets.fill(0)', 'm_targetGroup=vault::interaction::TargetIndex::None',
                      'm_targetIndex.clear()'):
            self.assertIn(token, reset)
        # Do not build/allocate a second target index for inactive categories.
        self.assertNotIn('m_targetIndex.build', reset)
