"""Exercise release failure boundaries without publishing anything."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import publish_release
from release_version import select_version, validate_version
from publish_release import publication_version


COMMIT = "0123456789abcdef" * 2 + "01234567"


class Versions(unittest.TestCase):
    def test_tags_and_preview_identity(self):
        self.assertEqual(select_version("refs/tags/v1.2.3-rc.1", COMMIT), "1.2.3-rc.1")
        self.assertEqual(select_version("refs/heads/master", COMMIT), "0.0.0-dev.g0123456789ab")
        self.assertEqual(validate_version(select_version("refs/heads/master", "0" * 40)), "0.0.0-dev.g000000000000")
        self.assertEqual(select_version("refs/heads/master", COMMIT, "2.0.0"), "2.0.0")

    def test_invalid_or_mismatched_tags_fail_closed(self):
        for version in ("v1.2.3", "1.2", "01.2.3", "1.2.3-01", "1.2.3/evil", "1.2.3\nnext=bad"):
            with self.subTest(version=version), self.assertRaises(ValueError):
                validate_version(version)
        with self.assertRaises(ValueError):
            select_version("refs/tags/v1.2.3", COMMIT, "1.2.4")
        with self.assertRaises(ValueError):
            select_version("refs/tags/v1.2.3", "bad")

    def test_master_auto_and_explicit_manual_publication(self):
        dev = select_version("refs/heads/master", COMMIT)
        self.assertEqual(publication_version("push", "refs/heads/master", COMMIT, dev, False), dev)
        self.assertEqual(publication_version("workflow_dispatch", "refs/heads/master", COMMIT, "1.2.3", True), "1.2.3")
        for event, ref, version, enabled in (
                ("push", "refs/heads/master", "1.2.3", False),
                ("workflow_dispatch", "refs/heads/master", dev, False),
                ("workflow_dispatch", "refs/heads/feature", dev, True),
                ("pull_request", "refs/pull/7/merge", dev, True)):
            with self.subTest(event=event, ref=ref), self.assertRaises(ValueError):
                publication_version(event, ref, COMMIT, version, enabled)


class Publication(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        verifier = patch.object(publish_release, "verify_tag")
        self.verify_tag = verifier.start()
        self.addCleanup(verifier.stop)
        self.directory = Path(self.temp.name)
        self.version = "1.2.3"
        for platform, suffix in (("linux", ".tar.gz"), ("linux", ".deb"), ("windows", ".zip")):
            (self.directory / f"DrivOnix-{self.version}-{platform}-x86_64{suffix}").write_bytes(suffix.encode())

    def upload_record(self):
        return {"assets": [{"name": p.name, "size": p.stat().st_size, "state": "uploaded",
                            "digest": "sha256:" + hashlib.sha256(p.read_bytes()).hexdigest()}
                           for p in self.directory.iterdir()]}

    def publish(self):
        publish_release.publish(self.directory, "owner/repo", "refs/tags/v" + self.version, COMMIT, self.version)

    def test_complete_upload_is_published_after_checksums(self):
        with patch.object(publish_release, "release", side_effect=lambda *args: None if not gh.called else self.upload_record()), \
             patch.object(publish_release, "gh") as gh:
            self.publish()
        calls = [c.args for c in gh.call_args_list]
        self.assertEqual([c[:2] for c in calls], [("release", "create"), ("release", "upload"), ("release", "edit")])
        self.assertIn("--draft", calls[0])
        self.assertIn("--verify-tag", calls[0])
        self.assertIn("--draft=false", calls[-1])
        self.assertIn("--latest=true", calls[-1])
        self.assertEqual(self.verify_tag.call_count, 2)
        self.assertEqual(len((self.directory / "SHA256SUMS").read_text().splitlines()), 3)

    def test_failed_upload_leaves_draft_unpublished(self):
        def command(*args):
            if args[:2] == ("release", "upload"):
                raise subprocess.CalledProcessError(1, "gh", stderr="upload failed")
        with patch.object(publish_release, "release", return_value=None), patch.object(publish_release, "gh", side_effect=command) as gh:
            with self.assertRaises(subprocess.CalledProcessError):
                self.publish()
        self.assertFalse(any(c.args[:2] == ("release", "edit") for c in gh.call_args_list))

    def test_prerelease_is_never_marked_latest(self):
        self.version = "1.2.3-rc.1"
        for path in list(self.directory.iterdir()):
            path.rename(path.with_name(path.name.replace("1.2.3", self.version)))
        with patch.object(publish_release, "release", side_effect=lambda *args: None if not gh.called else self.upload_record()), \
             patch.object(publish_release, "gh") as gh:
            self.publish()
        self.assertIn("--prerelease", gh.call_args_list[0].args)
        self.assertIn("--prerelease=true", gh.call_args.args)
        self.assertIn("--latest=false", gh.call_args.args)

    def test_existing_public_release_is_never_overwritten(self):
        with patch.object(publish_release, "release", return_value={"draft": False}), patch.object(publish_release, "gh") as gh:
            with self.assertRaises(ValueError):
                self.publish()
        gh.assert_not_called()

    def test_complete_same_source_release_is_a_successful_noop(self):
        names = [p.name for p in self.directory.iterdir()] + ["SHA256SUMS"]
        existing = {"draft": False, "target_commitish": COMMIT,
                    "assets": [{"name": name, "state": "uploaded", "size": 10,
                                "digest": "sha256:" + "a" * 64} for name in names]}
        # Fresh CPack timestamps can change bytes. Never replace the original
        # complete published release when rebuilding the same source.
        with patch.object(publish_release, "release", return_value=existing), patch.object(publish_release, "gh") as gh:
            self.publish()
        gh.assert_not_called()

    def test_master_manual_run_creates_tag_after_validating_source_and_packages(self):
        with patch.object(publish_release, "ensure_tag") as ensure, \
             patch.object(publish_release, "release", side_effect=lambda *args: None if not gh.called else self.upload_record()), \
             patch.object(publish_release, "gh") as gh:
            publish_release.publish(self.directory, "owner/repo", "refs/heads/master", COMMIT,
                                    self.version, event="workflow_dispatch", enabled=True)
        ensure.assert_called_once_with("owner/repo", "v1.2.3", COMMIT)
        self.assertIn("--draft=false", gh.call_args.args)

    def test_successful_master_push_publishes_its_development_build(self):
        self.version = select_version("refs/heads/master", COMMIT)
        for path in list(self.directory.iterdir()):
            path.rename(path.with_name(path.name.replace("1.2.3", self.version)))
        with patch.object(publish_release, "ensure_tag") as ensure, \
             patch.object(publish_release, "release", side_effect=lambda *args: None if not gh.called else self.upload_record()), \
             patch.object(publish_release, "gh") as gh:
            publish_release.publish(self.directory, "owner/repo", "refs/heads/master", COMMIT, self.version)
        ensure.assert_called_once_with("owner/repo", "v" + self.version, COMMIT)
        self.assertIn("--prerelease", gh.call_args_list[0].args)
        self.assertIn("--latest=false", gh.call_args.args)

    def test_tag_moved_during_upload_never_publishes(self):
        self.verify_tag.side_effect = [None, ValueError("tag moved")]
        with patch.object(publish_release, "release", side_effect=lambda *args: None if not gh.called else self.upload_record()), \
             patch.object(publish_release, "gh") as gh:
            with self.assertRaises(ValueError):
                self.publish()
        self.assertFalse(any(c.args[:2] == ("release", "edit") for c in gh.call_args_list))

    def test_draft_retry_verifies_uploaded_digests(self):
        def lookup(*args):
            if not gh.called:
                return {"draft": True, "target_commitish": COMMIT}
            result = self.upload_record()
            result["assets"][0]["digest"] = "sha256:" + "0" * 64
            return result
        with patch.object(publish_release, "release", side_effect=lookup), patch.object(publish_release, "gh") as gh:
            with self.assertRaises(ValueError):
                self.publish()
        self.assertEqual(gh.call_args_list[0].args[:2], ("release", "upload"))
        self.assertFalse(any(c.args[:2] == ("release", "edit") for c in gh.call_args_list))

    def test_missing_package_and_preview_ref_never_reach_github(self):
        next(self.directory.iterdir()).unlink()
        with patch.object(publish_release, "gh") as gh:
            with self.assertRaises(ValueError):
                self.publish()
            with self.assertRaises(ValueError):
                publish_release.publish(self.directory, "owner/repo", "refs/heads/master", COMMIT, self.version)
        gh.assert_not_called()


class RemoteIdentity(unittest.TestCase):
    def test_missing_auto_tag_is_created_at_the_tested_commit(self):
        missing = subprocess.CalledProcessError(1, "gh", stderr="gh: Not Found (HTTP 404)")
        with patch.object(publish_release, "gh", side_effect=[missing, "created"]) as gh, \
             patch.object(publish_release, "verify_tag") as verify:
            publish_release.ensure_tag("owner/repo", "v1.2.3", COMMIT)
        self.assertIn("--method", gh.call_args.args)
        self.assertIn("POST", gh.call_args.args)
        self.assertIn("ref=refs/tags/v1.2.3", gh.call_args.args)
        self.assertIn("sha=" + COMMIT, gh.call_args.args)
        verify.assert_called_once_with("owner/repo", "v1.2.3", COMMIT)

    def test_existing_auto_tag_is_verified_and_never_moved(self):
        with patch.object(publish_release, "gh", return_value="exists") as gh, \
             patch.object(publish_release, "verify_tag", side_effect=ValueError("wrong commit")):
            with self.assertRaises(ValueError):
                publish_release.ensure_tag("owner/repo", "v1.2.3", COMMIT)
        self.assertEqual(gh.call_count, 1)

    def test_authentication_failure_is_not_a_missing_tag(self):
        forbidden = subprocess.CalledProcessError(1, "gh", stderr="gh: Forbidden (HTTP 403)")
        with patch.object(publish_release, "gh", side_effect=forbidden) as gh:
            with self.assertRaises(subprocess.CalledProcessError):
                publish_release.ensure_tag("owner/repo", "v1.2.3", COMMIT)
        self.assertEqual(gh.call_count, 1)

    def test_draft_lookup_includes_all_authenticated_pages(self):
        draft = {"tag_name": "v1.2.3", "draft": True, "target_commitish": COMMIT}
        with patch.object(publish_release, "gh", return_value=json.dumps([[{"tag_name": "v9.0.0"}], [draft]])) as gh:
            self.assertEqual(publish_release.release("owner/repo", "v1.2.3"), draft)
        self.assertIn("--paginate", gh.call_args.args)
        self.assertIn("--slurp", gh.call_args.args)

    def test_annotated_and_lightweight_tags_resolve_to_tested_commit(self):
        tag = {"object": {"type": "tag", "sha": "a" * 40}}
        commit = {"object": {"type": "commit", "sha": COMMIT}}
        with patch.object(publish_release, "gh", side_effect=[json.dumps(tag), json.dumps(commit)]) as gh:
            publish_release.verify_tag("owner/repo", "v1.2.3", COMMIT)
        self.assertEqual(gh.call_count, 2)
        with patch.object(publish_release, "gh", return_value=json.dumps(commit)):
            publish_release.verify_tag("owner/repo", "v1.2.3", COMMIT)
            with self.assertRaises(ValueError):
                publish_release.verify_tag("owner/repo", "v1.2.3", "a" * 40)


if __name__ == "__main__":
    unittest.main()
