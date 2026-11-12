import importlib.util
import pathlib
import unittest

spec = importlib.util.spec_from_file_location("catalog", pathlib.Path(__file__).with_name("github_release_catalog.py"))
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)

A = "a" * 64


class FakeGitHub:
    def releases(self, repo):
        if repo.lower() == "drakmor/shadowmountplus":
            return [{"tag_name": "1.7beta12", "published_at": "2026-11-08T20:02:14Z",
                     "prerelease": True, "assets": [
                         {"name": "shadowmountplus.elf", "size": 3600000, "state": "uploaded",
                          "digest": "sha256:" + A,
                          "browser_download_url": "https://github.com/drakmor/ShadowMountPlus/releases/download/1.7beta12/shadowmountplus.elf"}]}]
        return [{"tag_name": "v2.1-rc1", "prerelease": True,
                 "published_at": "2026-11-12T00:00:00Z",
                 "assets": [{"name": "another.elf", "size": 100, "state": "uploaded",
                             "digest": "sha256:" + A,
                             "browser_download_url": "https://github.com/org/another/releases/download/v2.1-rc1/another.elf"}]}]

    def sha256(self, asset, limit):
        return asset["digest"][7:]


class TestCatalog(unittest.TestCase):
    def setUp(self):
        self.base = {"name": "Upstream", "payloads": [
            {"id": "shadowmountplus", "name": "ShadowMountPlus", "version": "1.7beta11",
             "filename": "ShadowMountPlus_1.7beta11.elf", "last_update": "2026-10-29",
             "source": "https://github.com/drakmor/ShadowMountPlus/releases",
             "source_direct": "https://github.com/drakmor/ShadowMountPlus/releases/download/1.7beta11/ShadowMountPlus_1.7beta11.zip"},
            {"id": "another", "name": "Another", "version": "v1.5",
             "filename": "another_v1.5.elf", "source": "https://github.com/org/another/releases"},
            {"id": "pldmgrx", "name": "Payload Manager X", "version": "v1.1",
             "filename": "pldmgrx.elf", "source": "https://github.com/bsk193/ps5-payload-manager-x/releases"},
        ]}

    def test_all_repositories_and_prerelease(self):
        out, errs, success, checked, added = catalog.generate(self.base, FakeGitHub())
        self.assertEqual((success, checked, added), (2, 2, 2))
        self.assertFalse(errs)
        self.assertEqual(len(out["payloads"]), 5)
        self.assertEqual({p["version"] for p in out["payloads"][-2:]}, {"1.7beta12", "v2.1-rc1"})
        self.assertTrue(all(p["channel"] == "experimental" for p in out["payloads"][-2:]))
        self.assertTrue(all(p["checksum"] == A for p in out["payloads"][-2:]))

    def test_invalid_url_not_accepted(self):
        self.assertIsNone(catalog.github_repo("https://github.com.evil.test/user/repo/releases"))
        self.assertEqual(catalog.github_repo("https://github.com/drakmor/ShadowMountPlus/releases"), "drakmor/ShadowMountPlus")

    def test_does_not_update_manager_itself(self):
        out, _, _, _, _ = catalog.generate(
            {"payloads": [self.base["payloads"][-1]]}, FakeGitHub()
        )
        self.assertEqual(len(out["payloads"]), 1)

    def test_refuses_ambiguous_binary(self):
        item = {"filename": "test.elf", "source_direct": ""}
        self.assertEqual(catalog.match_asset({"name": "unrelated.elf"}, item, False), -1)


if __name__ == "__main__":
    unittest.main()
