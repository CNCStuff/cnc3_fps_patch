const std = @import("std");

pub fn build(b: *std.Build) void {
    const optimize = b.standardOptimizeOption(.{});
    const make_target: []const u8 = if (optimize == .Debug) "debug" else "release";

    const make = b.addSystemCommand(&.{ "make", make_target });
    b.default_step.dependOn(&make.step);

    const package = b.addSystemCommand(&.{ "make", "package" });
    const package_step = b.step("package", "Build a redistributable folder in zig-out/package");
    package_step.dependOn(&package.step);

    const verify = b.addSystemCommand(&.{ "make", "verify" });
    const verify_step = b.step("verify", "Verify PE architecture, imports, and proxy exports");
    verify_step.dependOn(&verify.step);

    const clean = b.addSystemCommand(&.{ "make", "clean" });
    const clean_step = b.step("clean", "Remove build outputs");
    clean_step.dependOn(&clean.step);
}
