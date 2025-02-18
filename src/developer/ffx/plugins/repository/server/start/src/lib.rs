// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Result},
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_repository_server_start_args::StartCommand,
    ffx_writer::Writer,
    fidl_fuchsia_developer_ffx::RepositoryRegistryProxy,
    fidl_fuchsia_developer_ffx_ext::RepositoryError,
    fidl_fuchsia_net_ext::SocketAddress,
    pkg::config as pkg_config,
    std::io::Write as _,
};

#[ffx_plugin(RepositoryRegistryProxy = "daemon::protocol")]
pub async fn start(
    cmd: StartCommand,
    repos: RepositoryRegistryProxy,
    #[ffx(machine = SocketAddress)] mut writer: Writer,
) -> Result<()> {
    start_impl(cmd, repos, &mut writer).await
}

#[derive(Debug, PartialEq, serde::Serialize, serde::Deserialize)]
struct ServerInfo {
    address: std::net::SocketAddr,
}

async fn start_impl(
    _cmd: StartCommand,
    repos: RepositoryRegistryProxy,
    writer: &mut Writer,
) -> Result<()> {
    match repos
        .server_start()
        .await
        .context("communicating with daemon")?
        .map_err(RepositoryError::from)
    {
        Ok(address) => {
            let address = SocketAddress::from(address);
            if writer.is_machine() {
                writer.machine(&ServerInfo { address: address.0 })?;
            } else {
                writeln!(writer, "Repository server is listening on {}\n", address)?;
            }

            Ok(())
        }
        Err(err @ RepositoryError::ServerAddressAlreadyInUse) => {
            if let Ok(Some(address)) = pkg_config::repository_listen_addr().await {
                ffx_bail!("Failed to start repository server on {}: {}", address, err)
            } else {
                ffx_bail!(
                    "Failed to start repository server: {}\n\
                    The server listening address is now unspecified. You can fix this\n\
                    with:\n\
                    $ ffx config set repository.server.listen '[::]:8083'\n\
                    $ ffx repository server start",
                    err
                )
            }
        }
        Err(RepositoryError::ServerNotRunning) => {
            ffx_bail!(
                "Failed to start repository server: {:#}",
                pkg::config::determine_why_repository_server_is_not_running().await
            )
        }
        Err(err) => {
            ffx_bail!("Failed to start repository server: {}", err)
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_developer_ffx::{RepositoryError, RepositoryRegistryRequest},
        fuchsia_async as fasync,
        futures::channel::oneshot::channel,
        std::net::Ipv4Addr,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_start() {
        let mut writer = Writer::new_test(None);

        let (sender, receiver) = channel();
        let mut sender = Some(sender);
        let repos = setup_fake_repos(move |req| match req {
            RepositoryRegistryRequest::ServerStart { responder } => {
                sender.take().unwrap().send(()).unwrap();
                let address = SocketAddress((Ipv4Addr::LOCALHOST, 0).into()).into();
                responder.send(&mut Ok(address)).unwrap()
            }
            other => panic!("Unexpected request: {:?}", other),
        });

        start_impl(StartCommand {}, repos, &mut writer).await.unwrap();
        assert_eq!(receiver.await, Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_start_machine() {
        let mut writer = Writer::new_test(Some(ffx_writer::Format::Json));

        let address = (Ipv4Addr::LOCALHOST, 1234).into();

        let (sender, receiver) = channel();
        let mut sender = Some(sender);
        let repos = setup_fake_repos(move |req| match req {
            RepositoryRegistryRequest::ServerStart { responder } => {
                sender.take().unwrap().send(()).unwrap();
                let address = SocketAddress(address).into();
                responder.send(&mut Ok(address)).unwrap()
            }
            other => panic!("Unexpected request: {:?}", other),
        });

        start_impl(StartCommand {}, repos, &mut writer).await.unwrap();
        assert_eq!(receiver.await, Ok(()));

        let info: ServerInfo = serde_json::from_str(&writer.test_output().unwrap()).unwrap();
        assert_eq!(info, ServerInfo { address },);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_start_failed() {
        let mut writer = Writer::new_test(None);

        let (sender, receiver) = channel();
        let mut sender = Some(sender);
        let repos = setup_fake_repos(move |req| match req {
            RepositoryRegistryRequest::ServerStart { responder } => {
                sender.take().unwrap().send(()).unwrap();
                responder.send(&mut Err(RepositoryError::ServerNotRunning)).unwrap()
            }
            other => panic!("Unexpected request: {:?}", other),
        });

        assert!(start_impl(StartCommand {}, repos, &mut writer).await.is_err());
        assert_eq!(receiver.await, Ok(()));
    }
}
