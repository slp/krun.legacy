#[macro_export]
macro_rules! trace {
    ($($arg:tt)+) => {
        #[cfg(feature = "tracing")]
        ::tracing::trace!($($arg)+);
    };
}
pub use crate::trace;

#[macro_export]
macro_rules! debug {
    ($($arg:tt)+) => {
        #[cfg(feature = "tracing")]
        ::tracing::debug!($($arg)+);
    };
}
pub use crate::debug;

#[macro_export]
macro_rules! info {
    ($($arg:tt)+) => {
        #[cfg(feature = "tracing")]
        ::tracing::info!($($arg)+);
    };
}
pub use crate::info;

#[macro_export]
macro_rules! warn {
    ($($arg:tt)+) => {
        #[cfg(feature = "tracing")]
        ::tracing::warn!($($arg)+);
    };
}
pub use crate::warn;

#[macro_export]
macro_rules! error {
    ($($arg:tt)+) => {
        #[cfg(feature = "tracing")]
        ::tracing::error!($($arg)+);
    };
}
pub use crate::error;
