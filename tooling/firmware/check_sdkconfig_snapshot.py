"""Ensure historical config hashes tolerate only the proven disabled comment."""
from pathlib import Path
import hashlib,unittest
from sdkconfig_snapshot import historical_sdkconfig_bytes,SECTION,DISABLED
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
class ConfigSnapshotContracts(unittest.TestCase):
    def test_current_config_recovers_exact_known_preconfigure_snapshot(self):
        from check_auto_power import assert_config
        assert_config(self,'sdkconfig')
    def test_precise_disabled_block_is_only_change(self):
        prefix=b'CONFIG_UNRELATED=y\r\n'+SECTION
        suffix=b'CONFIG_CODEC_ES8311_SUPPORT=y\r\n'
        self.assertEqual(historical_sdkconfig_bytes(prefix+DISABLED+suffix),prefix+suffix)
    def test_enabled_option_is_never_removed(self):
        data=SECTION+b'CONFIG_CODEC_I2C_BACKWARD_COMPATIBLE=y\r\n'
        self.assertEqual(historical_sdkconfig_bytes(data),data)
    def test_unrelated_comments_wrong_section_and_partial_block_are_unchanged(self):
        for data in (b'# default:\r\nCONFIG_OTHER=y\r\n',DISABLED,
                     SECTION+DISABLED.replace(b'# default:\r\n',b''),SECTION):
            with self.subTest(data=data):self.assertEqual(historical_sdkconfig_bytes(data),data)
    def test_duplicate_option_is_not_normalized(self):
        data=SECTION+DISABLED+DISABLED
        self.assertEqual(historical_sdkconfig_bytes(data),data)
if __name__=='__main__':unittest.main()
